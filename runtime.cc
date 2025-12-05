// cflat runtime library, including garbage collector.

#include <algorithm>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

//
// standard functions that can be called as `extern` from cflat programs.
//

// prints the value of `n` to standard out.
extern "C" int64_t print_num(int64_t n) { 
  std::cout << n << std::endl;
  return 0; 
}

// casts `n` to a char and prints it to standard out.
extern "C" int64_t print_char(int64_t n) { 
  std::cout << char(n);
  return 0; 
}

//
// built-in functions that are assumed to exist by the cflat compiler.
//

// x64 word size in bytes.
#define WORDSIZE 8

// prints a message to standard out and exits normally. we do it this way
// instead of outputting to standard err and existing abnormally because that
// would interfere with the gradescope autograder.
extern "C" void _cflat_panic(const char *message) {
  std::cout << message << std::endl;
  exit(0);
}

// zero out `num_words` words starting from `start` (going from low to high
// addresses).
extern "C" void _cflat_zero_words(void *start, int64_t num_words) {
  memset(start, 0, num_words * WORDSIZE);
}

// forward defs for garbage collector used by _cflat_init_gc and _cflat_alloc.
// `heap_size` is given in words and is determined by the environment variable
// `CFLAT_HEAP_WORDS`. `base_frame_ptr` holds a pointer to the base of the
// `main` function's stack frame (used to terminate walking the stack during
// gc). `gc_log` flags whether gc should output a log of its collections, as
// determined by the environment variable `CFLAT_GC_LOG`. all values are
// initialized by _cflat_init_gc.
static size_t heap_size;
static uintptr_t *from_space;
static uintptr_t *to_space;
static uintptr_t *bump_ptr;
static uintptr_t *base_frame_ptr;
static bool gc_log;

// helper for _cflat_init_gc: retrieve the value of an environment variable and
// return it as a string, using "" if the environment variable isn't set.
std::string get_env(const std::string& env_name) {
  const char* val = std::getenv(env_name.c_str());
  return val ? val : "";
}

// forward decl for the garbage collector function
static void gc_collect(uintptr_t *top_frame_ptr);

// set base_frame_ptr, reads env vars, validates heap size, mallocs heap space
// initializes from_space, to_space, and bump_ptr
extern "C" void _cflat_init_gc() {
  assert(!from_space && !to_space && !bump_ptr && !base_frame_ptr &&
    "_cflat_init_gc should be called exactly once, at the beginning of main");

  // initialize `base_frame_ptr` to the base of the `_start` function's stack
  // frame (assumes we're being called from `main`).
  base_frame_ptr = (uintptr_t*)__builtin_frame_address(2);

  // check whether gc should print a log of its collections, as determined by
  // whether `CFLAT_GC_LOG` exists as an environment variable and if so whether
  // its value is "1".
  std::string gc_log_str = get_env("CFLAT_GC_LOG");
  gc_log = gc_log_str == "1";

  // retrieve the value of `CFLAT_HEAP_WORDS` as a string.
  std::string heap_size_str = get_env("CFLAT_HEAP_WORDS");  
  if (heap_size_str == "") {
    _cflat_panic("The CFLAT_HEAP_WORDS environment variable must be set to the desired size of the heap (in words).");
  }

  // initialize `heap_size` from the string retrieved from the environment,
  // checking that it is a legal value.
  if (std::all_of(heap_size_str.cbegin(), heap_size_str.cend(), ::isdigit)) {
    heap_size = stoi(heap_size_str, nullptr, 10);
  }
  if (heap_size == 0 || heap_size % 2 == 1) {
    _cflat_panic("CFLAT_HEAP_WORDS must contain a positive even number with no trailing spaces.");
  }

  // initialize from_space, to_space, and bump_ptr.
  from_space = (uintptr_t*)malloc(heap_size * WORDSIZE);
  if (!from_space) { _cflat_panic("unsuccessful allocation of heap."); }
  to_space = from_space + heap_size / 2;
  bump_ptr = from_space;

  if (gc_log) { std::cout << "_cflat_init_gc: allocated heap of " << heap_size << " words" << std::endl; }
}

// Check if bump_ptr + num_words fits within the current from-space half
// If yes: bump, zero, return.
// If no: trigger GC
//    check if fits: then bump, zero, return.
//    if not: log “out of memory” and call _cflat_panic.
extern "C" void* _cflat_alloc(size_t num_words) {
  assert(from_space && to_space && bump_ptr && base_frame_ptr &&
    "_cflat_alloc should only be called after _cflat_init_gc");

  // Current semispace boundaries
  uintptr_t *from_end   = from_space + heap_size / 2;
  
  auto has_space = [&](size_t n) {
    return bump_ptr + n <= from_end;
  };

  // First attempt: try to allocate without collecting
  if (gc_log) {
    std::cout << "_cflat_alloc: attempting to allocate "
              << num_words << " words...";
  }

  // successful allocation without GC
  if (has_space(num_words)) {
    if (gc_log) std::cout << "successful" << std::endl;

    uintptr_t *result = bump_ptr;
    bump_ptr += num_words; // bump allocation pointer
    _cflat_zero_words(result, num_words); // zero out allocated space
    return (void*)result;
  }

  // need to trigger GC
  if (gc_log) {
    std::cout << "triggering collection" << std::endl;
  }

  // Get the topmost frame pointer: the caller of _cflat_alloc
  uintptr_t *top_frame_ptr = (uintptr_t*)__builtin_frame_address(1);
  gc_collect(top_frame_ptr);
  
  // After GC, try to allocate again
  // Recompute boundaries in case from_space changed
  from_end   = from_space + heap_size / 2;
  
  // Second attempt: try again after GC
  if (gc_log) {
    std::cout << "_cflat_alloc: second attempt to allocate "
              << num_words << " words...";
  }

  // successful allocation after GC
  if (has_space(num_words)) {
    if (gc_log) std::cout << "successful" << std::endl;

    uintptr_t *result = bump_ptr;
    bump_ptr += num_words; // bump allocation pointer
    _cflat_zero_words(result, num_words); // zero out allocated space
    return (void*)result;
  }

  // out of memory
  _cflat_panic("out of memory");
  return nullptr; // unreachable
}



//
// the garbage collector implementation.
//

// NOTE: Tag values based on actual compiler behavior
static const uintptr_t TAG_STRUCT_ATOMIC = 0;
static const uintptr_t TAG_STRUCT_PTRS   = 4; 
static const uintptr_t TAG_ARRAY_ATOMIC  = 2;
static const uintptr_t TAG_ARRAY_PTRS    = 6;

// Helper to check if a header value looks like a pointer (forwarding address)
// Forwarding addresses are actual memory addresses in to-space
static bool is_forwarding_pointer(uintptr_t header) {
    uintptr_t* new_start = to_space;
    uintptr_t* new_end   = to_space + heap_size / 2;
    return (header >= (uintptr_t)new_start && header < (uintptr_t)new_end);
}

static size_t get_payload_words(uintptr_t header) {
    long len = header >> 3;  // Upper 61 bits: length
    long tag = header & 0x7;  // Lower 3 bits: Tag
    
    // Tag 4 is used for structs with pointers (TS4 encoding)
    if (tag == TAG_STRUCT_PTRS) {
        long size = len >> 5;
        long ptr_offsets = len & 0x1F;
        return size;
    }
    
    // Tag 0 can be either atomic struct OR struct with pointers (TS3 encoding)
    if (tag == TAG_STRUCT_ATOMIC) {
        // Check if upper bits encode a size (struct with pointers)
        long size = len >> 5;
        if (size > 0) {
            // This is a struct with pointers using TS3 encoding
            return size;
        }
        // This is an atomic struct: len encodes size in 2-word chunks
        // len=1 → size=2
        return len * 2;
    }
    
    // For arrays: len is the array length
    return len;
}

// Log helper to match formatted output
// e.g. [Array, len = 1, ptrs = false]
static void print_header_log(uintptr_t header) {
    long len = header >> 3;
    long tag = header & 0x7;
    
    if (tag == TAG_ARRAY_ATOMIC || tag == TAG_ARRAY_PTRS) {
        std::cout << "[Array, len = " << len << ", ptrs = " 
                  << ((tag == TAG_ARRAY_PTRS) ? "true" : "false") << "]";
    } else if (tag == TAG_STRUCT_PTRS) {
        // Tag 4 is used for structs with pointers (TS4 encoding)
        long size = len >> 5;
        long ptr_bitmap = len & 0x1F;
        
        if (ptr_bitmap == 0) {
            std::cout << "[Struct, size = " << size << ", ptr offsets = none]";
        } else {
            // TS4: bitmap value N means first N+1 fields are pointers
            std::cout << "[Struct, size = " << size << ", ptr offsets =";
            size_t num_ptr_fields = ptr_bitmap + 1;
            for (size_t i = 0; i < num_ptr_fields && i < 5; ++i) {
                std::cout << " " << i;
            }
            std::cout << "]";
        }
    } else if (tag == TAG_STRUCT_ATOMIC) {
        // Tag 0: could be atomic struct OR struct with pointers (TS3 encoding)
        long size = len >> 5;
        long ptr_bitmap = len & 0x1F;
        
        if (size > 0) {
            // This is a struct with pointers using TS3 encoding (bitmap is actual bitmap)
            if (ptr_bitmap == 0) {
                std::cout << "[Struct, size = " << size << ", ptr offsets = none]";
            } else {
                // TS3: bitmap seems to be shifted - bit 0 represents offset 1, bit 1 represents offset 2, etc.
                std::cout << "[Struct, size = " << size << ", ptr offsets =";
                for (int i = 0; i < 5; ++i) {
                    if (ptr_bitmap & (1 << i)) {
                        std::cout << " " << (i + 1);  // Print i+1, not i
                    }
                }
                std::cout << "]";
            }
        } else {
            // Atomic struct: len encodes size in 2-word chunks
            std::cout << "[Struct, size = " << (len * 2) << ", ptr offsets = none]";
        }
    } else {
        // Unknown tag
        std::cout << "[Unknown tag " << tag << ", len = " << len << "]";
    }
}

// Process a pointer (forward or copy)
// slot_ptr: address of the pointer variable (root or field in heap object)
// free_ptr: reference to the current allocation pointer in to_space
static void process_transitive(uintptr_t* slot_ptr, uintptr_t*& free_ptr) {
  uintptr_t obj_addr = *slot_ptr;
  // 1. Filter: Check if pointer is NULL or outside from_space
  if (obj_addr == 0) return;
  // Calculate boundaries of the space we are removing from
  uintptr_t* old_start = from_space;
  uintptr_t* old_end   = from_space + heap_size / 2;
  // If it's not in the old heap, we don't move it
  if (obj_addr < (uintptr_t)old_start || obj_addr >= (uintptr_t)old_end) {
      return;
  }

  uintptr_t* obj_ptr = (uintptr_t*)obj_addr;
  uintptr_t* header_ptr = obj_ptr - 1; // header was written 8 bytes before the data pointer
  uintptr_t header = *header_ptr;

  uintptr_t* new_start = to_space;
  uintptr_t* new_end   = to_space + heap_size / 2;
  // If the header is a pointer into the new space, it's forwarded
  // If the object was already moved, the header now contains the address of the copy: value will fall inside the to_space range
  if (header >= (uintptr_t)new_start && header < (uintptr_t)new_end) {
    // Update the slot (current root) to point to point to the address found in the header
    *slot_ptr = header;

    if (gc_log) {
        long old_rel = ((uintptr_t)obj_ptr - (uintptr_t)from_space) / WORDSIZE;
        // The forwarded address (header) points to the new data location
        uintptr_t* forwarded_addr = (uintptr_t*)header;
        long new_rel = ((uintptr_t)forwarded_addr - (uintptr_t)to_space) / WORDSIZE;

        std::cout << "---- copying object at relative address " << old_rel 
                  << " with header [Forwarded]" << std::endl;
        std::cout << "---- object forwarded to relative address " << new_rel << std::endl;
    }

    return;
  }

  size_t payload_words = get_payload_words(header);

  if (gc_log) {
    long rel_addr_from = ((uintptr_t)obj_ptr - (uintptr_t)from_space) / WORDSIZE;
    uintptr_t* dest_obj_ptr = free_ptr + 1;
    long rel_addr_to = ((uintptr_t)dest_obj_ptr - (uintptr_t)to_space) / WORDSIZE;

    std::cout << "---- copying object at relative address " << rel_addr_from 
              << " with header ";
    print_header_log(header);
    std::cout << std::endl;
    
    std::cout << "---- moving object from relative address " << rel_addr_from 
              << " to " << rel_addr_to << std::endl;
  }

  // 2. Not forwarded yet: copy the object to to_space

  // Copy the whole block (header + data)
  // free_ptr points to the destination address for the Header
  uintptr_t* dest_header_ptr = free_ptr;
  uintptr_t* dest_obj_ptr    = free_ptr + 1; // The new pointer value

  // Total size = 1 (header) + len (payload).
  size_t copy_size_words = 1 + payload_words;

  // Perform copy
  std::memcpy(dest_header_ptr, header_ptr, copy_size_words * WORDSIZE);

  // Leaving a trail for future references and updating the current reference:
  // 4. Install Forwarding Address
  // Overwrite old header with the memory address of the new copy in ToSpace (pointer to data, not header)
  // If another variable also points to this old object in FromSpace, it can find the new location
  // knows to just update it to this address rather than copying the object again
  *header_ptr = (uintptr_t)dest_obj_ptr;
  
  // 5. Update Root to point to correct new location
  *slot_ptr = (uintptr_t)dest_obj_ptr;

  // 6. Bump Free Pointer
  free_ptr += copy_size_words;

}

// Main GC entry point
static void gc_collect(uintptr_t* top_frame) {
  // Current allocation pointer in the to-space
  uintptr_t* free_ptr = to_space;
  // Scan pointer in the to-space
  uintptr_t* scan_ptr = to_space;

  // 1. Stack Scanning (Roots)
  uintptr_t* frame = top_frame;
  int frame_idx = 0;
  // Walk up the stack until we hit the base frame (main)
  // We traverse until frame >= base_frame_ptr (stop BEFORE the C runtime frame)

  while (frame < base_frame_ptr) {
    // gc_root_count (num pointer vars in curr stack frame) is stored at -8(%rbp) --> first word of the frame
    // frame pointer points to old %rbp
    int64_t gc_root_count = *((int64_t*)(frame - 1));
    if (gc_log) {
            std::cout << "gc: processing stack frame " << frame_idx 
                      << " (from top of stack), with " << gc_root_count << " pointers" << std::endl;
        }
    // Roots are stored below the GC header
    // GC header is at -1 word
    // First root (index 0) is at -2 words (-16 bytes) -- > root i is at frame - 2 - i
    for (size_t i = 0; i < gc_root_count; ++i) {
      if (gc_log) {
          std::cout << "-- processing pointer offset " << i << std::endl;
      }
      uintptr_t* root_slot = frame - 2 - i;
      process_transitive(root_slot, free_ptr);
    }

    // Move to next frame (stored at 0(%rbp))
    if (frame == base_frame_ptr) break; // if loop reaches base_frame_ptr, it has finished scanning main
    frame = (uintptr_t*)*frame; // next frame pointer: retrieves the address of the caller's frame
    frame_idx++;
  }

  // 2. Scan (Trace)
  if (gc_log) {
      std::cout << "gc: starting scan" << std::endl;
  }
  // scan_ptr points to the start of the Header of the object to scan
  // free_ptr points to the next free word

  while (scan_ptr < free_ptr) {
    uintptr_t header = *scan_ptr;
    size_t payload_words = get_payload_words(header);
    long tag = header & 0x7; // Lower 3 bits: The Tag (type information, e.g., is it a pointer array?)

    if (gc_log) {
      std::cout << "-- scanning header ";
      print_header_log(header);
      std::cout << std::endl;
    }
    // Process each field in the object (scan_ptr + 1)
    uintptr_t* fields = scan_ptr + 1;

    // If this object contains pointers, scan them
    if (tag == TAG_ARRAY_PTRS) {
        // Arrays with pointers: all elements are pointers
        for (size_t i = 0; i < payload_words; ++i) {
            process_transitive(&fields[i], free_ptr);
        }
    } else if (tag == TAG_STRUCT_PTRS) {
        // TS4: bitmap value N means first N+1 fields are pointers
        long len = header >> 3;
        long ptr_bitmap = len & 0x1F;
        
        if (ptr_bitmap > 0) {
            size_t num_ptr_fields = ptr_bitmap + 1;
            for (size_t i = 0; i < num_ptr_fields && i < payload_words; ++i) {
                process_transitive(&fields[i], free_ptr);
            }
        }
    } else if (tag == TAG_STRUCT_ATOMIC) {
        // Tag 0: check if it's actually a struct with pointers (TS3 encoding)
        long len = header >> 3;
        long size = len >> 5;
        long ptr_bitmap = len & 0x1F;
        
        if (size > 0 && ptr_bitmap > 0) {
            // TS3: bitmap is shifted - bit 0 represents offset 1, bit 1 represents offset 2, etc.
            for (size_t i = 0; i < payload_words && i < 5; ++i) {
                // Check bit i for offset i+1
                if (i < payload_words - 1 && (ptr_bitmap & (1 << i))) {
                    process_transitive(&fields[i + 1], free_ptr);
                }
            }
        }
    }
    // Advance scan_ptr to the next object header
    // Current object size = 1 (header) + len (data)
    size_t size = 1 + payload_words;
    if (gc_log) {
      std::cout << "-- incrementing scanning ptr by " << size << std::endl;
    }
    scan_ptr += size;
  }


  // 3. Cleanup and Swap
  // Calculate live size for log
  size_t live_words = free_ptr - to_space;
  if (gc_log) {
    std::cout << "gc: swapping from and to spaces (" << live_words 
              << " words still live)" << std::endl;
  }

  // Swap spaces
  std::swap(from_space, to_space);
  // Reset bump_ptr to the end of the data just copied (now in from_space)
  bump_ptr = from_space + live_words;

}
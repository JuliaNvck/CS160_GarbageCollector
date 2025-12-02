// cflat runtime library, including garbage collector.

#include <algorithm>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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
  uintptr_t *from_start = from_space;
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
  from_start = from_space;
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
  if (gc_log) {
    std::cout << "out of memory" << std::endl;
  }
  _cflat_panic("out of memory");
  return nullptr; // unreachable
}



//
// the garbage collector implementation.
//

// Forward decls for helpers
static bool in_from_space(uintptr_t *p);
static uintptr_t* copy_or_forward(uintptr_t **slot,
                                  uintptr_t *&new_bump);
static void scan_copied_objects(uintptr_t *scan_ptr,
                                uintptr_t *&new_bump);

static bool in_from_space(uintptr_t *p) {
  uintptr_t *from_start = from_space;
  uintptr_t *from_end   = from_space + heap_size / 2;
  return p >= from_start && p < from_end;
}

// Main GC entry point
static void gc_collect(uintptr_t *top_frame_ptr) {
  // Initialize to-space bump and scan pointers
  uintptr_t *to_start = to_space;
  uintptr_t *scan_ptr = to_start;
  uintptr_t *new_bump = to_start;

  // Scan stack frames from top_frame_ptr down to base_frame_ptr

}
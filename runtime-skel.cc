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

extern "C" void* _cflat_alloc(size_t num_words) {
  assert(from_space && to_space && bump_ptr && base_frame_ptr &&
    "_cflat_alloc should only be called after _cflat_init_gc");
  
    // FILL ME IN
    //
    // note: to get a pointer to the base of the topmost stack frame (i.e., the
    // stack frame of the function that triggered GC), use
    // `(uintptr_t*)__builtin_frame_address(1)`. this _must_ be called from
    // _cflat_alloc, _not_ from a function called by _cflat_alloc.
}

//
// the garbage collector implementation.
//

// FILL ME IN
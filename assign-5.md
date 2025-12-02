# Assignment 5: Garbage Collection

__RELEASED:__ Friday, Nov 28
__DUE:__ Friday, Dec 12
__LATE DEADLINE 1:__ N/A
__LATE DEADLINE 2:__ N/A

You may work in groups of 1--3 people. If working in a group of 2--3, be sure to indicate that when submitting to Gradescope.

## Description

Implement a semispace garbage collector for CFlat, using Cheney's algorithm for tracing the heap. Your solution should fill out the runtime library skeleton code provided with this assignment. DO NOT MODIFY any existing code in the skeleton; you may otherwise add any code you wish. Your implementation should be entirely within this one file (renamed from `runtime-skel.cc` to `runtime.cc`), and it should be the only thing submitted.

The skeleton code reads the values of two environment variables:

- `CFLAT_HEAP_WORDS` specifies the size of the heap;
- `CFLAT_GC_LOG` determines whether to output a log of the collector's actions and will set the global `gc_log` to true or false accordingly.

See the `Reference Solution` section for more details on those environment variables and how to use them.

IMPORTANT NOTE: Unlike previous assignments, we will not provide a plethora of student-facing tests. Instead, we will provide one test case per test suite (as described in the `Grading` section) and you are responsible for writing more test cases in order to test your solution. You may all collaborate on writing test cases and share them with the rest of the class if you wish (you are not obligated to do so). See the section `Writing Tests` for some important information on writing useful test cases. In the provided tests, the solution file name contains the heap size for that run (e.g., `test-example.16.soln` means a heap size of 16 words).

## Input/Output Specifications

There is no input to your collector other than the calls to the library functions from the linked CFlat program and the environment variables described above.

If `gc_log` is false then your collector has no output unless the program runs out of memory, in which case it should call `_cflat_panic("out of memory")` (provided in the skeleton code).

If `gc_log` is true then your collector should output a log of its actions that matches the log output of my implementation on CSIL (as compared with `diff -wB`). The format should be self-explanatory from looking at examples, except for the notion of "relative address". A relative address is the offset from `from_ptr` (if the object is in "from" space) or `to_ptr` (if the object is in "to" space). Note that the printed relative address is that of the first word of the data, not the header word. This definition of relative address allows us to print consistent addresses for objects even though the absolute addresses may vary between executions.

## Grading

The grading will be done using a set of test suites, weighted evenly. To describe the test suites, we use the following terminology:

- single-frame: `main` is the only stack frame on the stack
- multi-frame: there is more than one stack frame on the stack (i.e., there have been a series of function calls)

- single-level: the only pointers are on the stack, no heap object contains a pointer
- multi-level: heap objects can contain pointers to other heap objects

- no-alias: each heap object has at most one pointer to it
- aliasing: a heap object may have multiple pointers to it

The test suites for this assignment are:

- `TS1`: single-frame, single-level, no-alias
- `TS2`: single-frame, single-level, aliasing
- `TS3`: single-frame, multi-level, no-alias
- `TS4`: single-frame, multi-level, aliasing
- `TS5`: multi-frame, single-level, no-alias
- `TS6`: multi-frame, single-level, aliasing
- `TS7`: multi-frame, multi-level, no-alias
- `TS8`: multi-frame, multi-level, aliasing

In all test suites the allocated heap objects may be integers, structs, or arrays. Some tests will complete normally while others will run out of memory.

## Writing Tests

You will be writing your own test cases for GC (with some example test cases provided by us). Since you have my implementation available, one option is to write whatever program you want that matches the desired test suite, then run my implementation and your own and compare the logs. However, if you're trying to trace through your test case and understand why the collector is taking the actions it logs, there's a slight wrinkle. This is best explained by example, so consider the following CFlat program:

```
fn main() -> int {
  let a:&int;
  a = new int;
  a = nil;
  return 0;
}
```

If we compile the program to LIR we get:

```
fn main() -> int {
let _const_0:int, _tmp1:&int, a:&int

entry:
  _const_0 = $const 0
  _tmp1 = $alloc_single int
  a = $copy _tmp1
  a = $copy __NULL
  $ret _const_0
}
```

Notice that lowering to LIR has introduced a temporary pointer variable `_tmp1`, and even though `a` has been set to `__NULL` (and hence no longer points to the allocated heap object) `_tmp1` still points to that object. Hence the object is still live at the `return` statement, even though in the CFlat program it looks dead. These temporary variables introduced by lowering mean that just looking at a CFlat program doesn't necessarily tell you when a heap object is dead, which can be confusing when comparing the program to the collector's log.

The easiest fix is to compile the program to LIR, then manually edit the LIR code to either (1) remove the temporary variable; or (2) set it to __NULL when it's no longer needed. Either method will prevent this problem from occurring. You can then compile the fixed LIR code (instead of the original CFlat program) to get an executable.

Fixed example using method (1):

```
fn main() -> int {
let _const_0:int, a:&int

entry:
  _const_0 = $const 0
  a = $alloc_single int
  a = $copy _tmp1
  $ret _const_0
}
```

Fixed example using method (2):

```
fn main() -> int {
let _const_0:int, _tmp1:&int, a:&int

entry:
  _const_0 = $const 0
  _tmp1 = $alloc_single int
  a = $copy _tmp1
  _tmp1 = $copy __NULL
  a = $copy __NULL
  $ret _const_0
}
```

For example, the example test cases we provide have two files each: `test-example.cb` and `test-example-fixed.lir`. If you compile `text-example.cb` to LIR then the LIR you get is not the same as `test-example-fixed.lir`, which has been manually edited to fix the issue per the description above. To get the behavior specified in the top-level comment in `test-example.cb` you need to compile `test-example-fixed.lir` with `runtime.o`, i.e., `cflat -r runtime.o test-example-fixed.lir`.

Also, remember when you're selecting heap sizes that the allocatable space is 1/2 the total heap size. If you want your test to trigger GC after allocating 12 words, you need to specify a heap size of 24 words.

## Reference Solution

I have placed an executable of my own Cflat compiler implementation on CSIL in `~benh/160/cflat`, along with an object file for the runtime library that uses my GC implementation called `runtime.o`. Use `cflat --help` to see how to use it. In particular for this assignment:

- Compile a program using `cflat -r runtime.o <file>.cb` (where `runtime.o` can be either the one provided in my directory or your own implementation). Also remember that `cflat` can take `*.lir` files to compile, not just `*.cb` files; this matters due to what's in the `Writing Tests` section.

- When running a CFlat executable, you must set the environment variable `CFLAT_HEAP_WORDS` to specify the size of the heap. Remember that because we're using a semispace collector, the amount of available memory for allocation is half the size of the heap. Optionally, you can set the environment variable `CFLAT_GC_LOG` in order to have the GC implementation print out a log of its activity (this is the output that Gradescope uses to grade your submission).

    - EXAMPLE: if the CFlat executable is called `prog`, you could run it with `CFLAT_HEAP_WORDS=16 ./prog` or `CFLAT_GC_LOG=1 CFLAT_HEAP_WORDS=16 ./prog`.

You can use the reference solution to test your code before submitting. If you have any questions about the output format or the behavior of the implementation you can answer them using the reference solution; this is the solution that Gradescope will use to test your submissions.

## Submitting to Gradescope

The autograder is running Ubuntu 22.04 and has the latest `build-essential` package installed (including `g++` version 11.4.0 and the `make` utility). You may not use any other libraries or packages for your code. Your submission must meet the following requirements:

- There must be a file called `runtime.cc` that contains your GC implementation; this should be the only file submitted.

- The submitted files are not allowed to contain any binaries, and your solution is not allowed to use the network.

## Use AddressSanitizer

Student programs can often contain memory errors that happen not to manifest themselves when the students execute their code locally, but do manifest themselves when the program is uploaded and executed by Gradescope. This difference in behavior can result in confusion because Gradescope is claiming there is an error, but there is seemingly no error when the students run the program themselves (note that there actually is an error, it just happens to not be caught at runtime). To avoid this issue, please always compile your code using AddressSanitizer, which will catch most such errors locally. See the following site for an explanation of how to do so (the given flags are for the `clang` compiler, but they should be the same for `gcc`): https://github.com/google/sanitizers/wiki/AddressSanitizer.

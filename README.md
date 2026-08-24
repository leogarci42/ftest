# ftest — a small C/C++ test library

Header-only testing framework for C and C++ code, built for this project but
usable by any program that compiles with a C++17 compiler on Linux.
It provides suites with colored output, stdout capture, RAII file helpers,
file-descriptor leak detection, heap-growth detection and a subprocess runner
with timeouts.

```
ftest/
├── ftest.hpp        umbrella include — #include this and you have everything
├── framework.hpp    colors, TestFailure, assertions, Suite, Runner
├── capture.hpp      OutputCapture / capture_stdout
├── files.hpp        FileDescriptor, TempFile, TempDir (RAII)
├── fdtrack.hpp      fd_snapshot, FdLeakGuard
├── alloc.hpp        AllocGuard, alloc_snapshot, running_under_asan
├── process.hpp      RunResult, describe_exit, CommandRunner
└── test_library.cpp self-test of the library itself (`make test-lib`)
```

## Quick start

```cpp
#include "ftest/ftest.hpp"

int main()
{
    ftest::Suite suite("my feature");

    suite.add("two plus two is four", [] {
            ftest::require(2 + 2 == 4, "math is broken");
    });
    suite.add("strings compare equal", [] {
            ftest::require_equal(std::string("abc"), std::string("abc"),
                                 "basic equality");
    });

    ftest::Runner runner;
    runner.add(std::move(suite));
    return runner.run_all();
}
```

Compile and link (nothing to link but your own code):

```sh
c++ -std=c++17 -I. my_tests.cpp -o my_tests
./my_tests   # exit code 0 = all green, 1 = at least one failure
```

Output looks like:

```
== series: my feature (2 tests)
  - two plus two is four [PASS]
  - strings compare equal [PASS]

ALL 2 TESTS PASSED
```

Colors: blue headers, green `[PASS]`, red `[FAIL]`, yellow `[ERROR]` for
unexpected exceptions. Every failing test prints its description followed by a
detailed message; one failure never stops the rest of the series.

## How it works

### Suites and the runner

`ftest::Suite` holds a name and a list of `(description, function)` cases.
`add()` returns `*this`, so registrations chain. Suites are **move-only**:
registering them into a `ftest::Runner` transfers ownership
(`runner.add(std::move(suite))`). `Runner::run_all()` executes every case in
its own try/catch block:

| thrown                     | shown as | effect                    |
|----------------------------|----------|---------------------------|
| `ftest::TestFailure`       | red FAIL | message printed verbatim  |
| any `std::exception`       | yellow ERROR | "unexpected exception: …" |
| anything else              | red ERROR | "unknown exception"       |

The return value of `run_all()` is a process exit code: `0` when everything
passed, `1` otherwise — drop it straight into CI or a Makefile.

### Assertions

All assertions throw `ftest::TestFailure` on failure; there are no error-code
returns to forget to check.

```cpp
require(cond, what);                       // generic boolean check
require_false(cond, what);
require_equal(got, expected, what);        // string and numeric overloads;
                                           // failure shows both values labeled
require_contains(haystack, needle, what);  // substring check, prints got

require_throws<std::runtime_error>(what, fn); // fn must throw something
require_no_throw(what, fn);                   // any escape fails the test
```

The string overload of `require_equal` renders failures as:

```
my check
       expected: [wanted value]
       got     : [actual value]
```

### Stdout capture (`capture.hpp`)

```cpp
{
    ftest::OutputCapture capture;
    printf("goes into the buffer\n");          // or write(1, ...), std::cout, …
    std::string text = capture.content();      // "goes into the buffer\n"
}                                              // real stdout restored here
```

Internally it `dup2`s a `tmpfile()` over `STDOUT_FILENO` in the constructor
and restores the saved descriptor in the destructor, so captures nest safely
with scopes and survive exceptions. `capture_stdout(fn)` is a convenience
wrapper returning everything `fn` printed.

Works for anything writing to fd 1: C `printf`, POSIX `write`,
`std::cout` (after `fflush`, which `content()` performs for you).

### File helpers (`files.hpp`)

```cpp
ftest::FileDescriptor fd(path);               // throws if open() fails,
ftest::FileDescriptor out(path, O_WRONLY);    // closes automatically

ftest::TempFile file("/tmp/x/probe.bin", "content"); // written on construction
const std::string& p = file.path();
// remove() runs in the destructor, even on test failure

ftest::TempDir dir("myprefix");              // mkdtemp-based, removed on scope end
```

Everything is RAII and non-copyable, which is exactly what makes the leak
guards below meaningful: cleanup happens even when an assertion throws.

### fd leak detection (`fdtrack.hpp`)

```cpp
ftest::FdLeakGuard guard("hashing session");
do_things_that_open_files();                  // or forget to close one…
guard.require_clean();                        // throws listing leaked fds:
                                              // "hashing session: 1 file
                                              //  descriptor(s) left open: 7"
```

How it works: `/proc/self/fd` contains one entry per open descriptor of the
current process. The guard snapshots those numbers when constructed and diffs
against a second snapshot inside `require_clean()`. Only *newly appearing*
descriptors count as leaks — descriptors that were already open before the
guard started (stdin/stdout, your harness) are ignored, and descriptors you
closed again are fine. Linux-only by design (reads procfs).

Typical pattern around a whole test body:

```cpp
suite.add("no descriptor leaks", [&] {
        ftest::FdLeakGuard guard("feature x");
        exercise_feature();
        guard.require_clean();
});
```

### Heap-growth detection (`alloc.hpp`)

```cpp
if (ftest::alloc_tracking_available())
{
    ftest::AllocGuard guard("parsing 200 KB");
    parse(big_input);
    guard.require_no_heap_growth();           // optional tolerance in bytes
}
```

On glibc this snapshots `mallinfo2()` (bytes in use + sbrk/mmapped bytes) at
construction and compares after. It catches forgotten `free()`s and unbalanced
C++ allocations because `operator new` routes through malloc. Two caveats,
both handled for you:

* Under AddressSanitizer, malloc is interposed and `mallinfo2` no longer sees
  reality — `alloc_tracking_available()` returns false then, and guards become
  no-ops. ASan's own leak sanitizer covers leaks in that build instead.
* Allocators may cache memory, so a strict zero tolerance can be too harsh for
  third-party libraries; pass a tolerance: `require_no_heap_growth(4096)`.

`running_under_asan()` tells you which mode you are in (compile-time macro
detection, so it also works for clang's `__has_feature`).

### Subprocess runner (`process.hpp`)

```cpp
ftest::CommandRunner app("./ft_ssl");

ftest::RunResult r = app.run(
        {"md5", "-q", "-s", "abc"},   // argv (program itself excluded)
        "optional stdin payload",      // piped to the child's stdin
        5000,                          // timeout in ms (default)
        0);                            // optional RLIMIT_NOFILE for the child

r.output      // everything the child wrote to stdout AND stderr (merged)
r.status      // raw waitpid status — use the WIFEXITED family on it
r.timed_out   // true when the child had to be SIGKILLed

ftest::describe_exit(r)   // human-readable: "exit code 3",
                          // "crashed: killed by signal SIGSEGV",
                          // "timed out (possible hang)"
```

How it works: `fork` + `execvp`, with pipes for stdin and merged
stdout+stderr. Writing the stdin payload and reading output use loops that
retry on `EINTR`. Reading is driven by `poll()` against a deadline; when it
expires the child gets `SIGKILL`, the pipe drains closed, and `waitpid`
reaps it — so a hanging child can never hang your test binary. The optional
rlimit is applied inside the child before `execvp`, letting you test resource
exhaustion behavior deterministically.

## Design notes

* **Header-only.** No .cpp files to compile, no build-system integration
  beyond an include path. Everything is `inline` / templates.
* **Exceptions as control flow.** A failing assertion unwinds to the suite
  loop, which means all the RAII helpers (captured stdout, temp files, open
  fds) unwind correctly too. That interplay is the core idea: tools clean up
  even when tests fail.
* **No macros for registration.** Tests are plain lambdas added with
  `suite.add("desc", [] { … })`; no `TEST()` preprocessor magic, no global
  static constructors, no single translation-unit tricks.
* **Process isolation where crashes live.** In-process tests are fast but a
  segfault kills the whole binary; use `CommandRunner` for anything that might
  crash or hang, and keep in-process suites for pure logic.

## Limitations

* fd tracking needs Linux (`/proc`).
* Allocation tracking needs glibc (`mallinfo2`) and is disabled under ASan.
* Colors are always emitted (fine for terminals and most CI logs).

## Self-test

The library tests itself — the leak detector proves it detects leaks, the
timeout kills a real hang, etc:

```sh
make test-lib
```

Run this whenever you change anything under `ftest/`.

# ftl / ftest — The Guide

An introduction for people (and agents) who want to *use* this library, not
just read its API table. For per-symbol details see
[reference.md](reference.md); for contributor rules see
[AGENTS.md](AGENTS.md).

---

## Table of contents

1. [What is this?](#1-what-is-this)
2. [Getting it into your project](#2-getting-it-into-your-project)
3. [Tutorial: your first test suite](#3-tutorial-your-first-test-suite)
4. [Assertions — choosing the right one](#4-assertions--choosing-the-right-one)
5. [Leak hunting: fds, heap, threads](#5-leak-hunting-fds-heap-threads)
6. [Testing binaries that crash or hang](#6-testing-binaries-that-crash-or-hang)
7. [ftl in production code](#7-ftl-in-production-code)
8. [Using ftl from plain C](#8-using-ftl-from-plain-c)
9. [Recipes](#9-recipes)
10. [Philosophy & rules of thumb](#10-philosophy--rules-of-thumb)

---

## 1. What is this?

Two header-only libraries that solve the same problem from two sides:
**low-level code is easy to get subtly wrong, so both production code and its
tests need the same small set of sharp tools** — subprocesses, descriptors,
temp files, threads, allocation visibility.

```
┌───────────────────────────────────────────────┐
│  ftest/   testing framework                   │   suites, assertions,
│           "make failures loud and precise"    │   leak guards, matchers
├───────────────────────────────────────────────┤
│  ftl/     low-level utilities                 │   subprocess, RAII fd,
│           "never leak, never hang"            │   pipes, temp dirs, threads
└───────────────────────────────────────────────┘
```

* `ftl/` is safe to use **in production code** — no test framework concepts
  leak into it.
* `ftest/` builds on `ftl/` and adds the test-runner machinery.
* Both are header-only: copy the directory, add one include path, done.
* Linux-first. Strict-POSIX fallbacks exist for everything else (see the
  portability table in the [README](README.md)).

### The one idea to remember

**Everything cleans up after itself, even when a test fails.** Assertions
throw; RAII objects catch the unwinding on their way out. That interplay —
throwing assertions + exception-safe helpers — is the whole design:

```cpp
suite.add("parses without leaking", [] {
        ftest::FdLeakGuard guard("parser");        // snapshot fds
        auto result = parse_file(make_input());    // TempFile cleans up…
        ftest::require_equal(result.lines, 42, "line count");
        guard.require_clean();                     // …even if require threw
});
```

If the assertion fails, the throw skips `require_clean()` — but the temp file
is still removed, the guard's destructor still runs, and the Suite loop
prints a red FAIL and moves on to the next test.

---

## 2. Getting it into your project

### Option A — drop-in headers

```sh
cp -r /path/to/ftest/ftl  myproject/vendor/
cp -r /path/to/ftest/ftest myproject/vendor/
```

```sh
c++ -std=c++17 -Ivendor -pthread app.cpp -o app
```

```cpp
#include <ftest/ftest.hpp>   // testing framework (pulls ftl in)
#include <ftl/ftl.hpp>       // just the utilities
#include <ftl/ftl.h>         // pure-C subset
```

### Option B — CMake (bigger projects)

```sh
cmake -S . -B build && cmake --install build --prefix ~/.local
```

```cmake
# consumer CMakeLists.txt
find_package(ftl REQUIRED)               # picks up the installed export
target_link_libraries(my_tests PRIVATE ftest::ftest)
target_link_libraries(my_app   PRIVATE ftl::ftl)
```

Or simply `add_subdirectory(ftest)` if you vendor it.

### Requirements

| | |
|---|---|
| Compiler | C++17 (`c++`, `g++`, `clang++`) for the C++ parts; C99 for `ftl.h` |
| OS | Linux recommended (procfs features); strict POSIX elsewhere with graceful degradation |
| Link | `-pthread` — that's all |

---

## 3. Tutorial: your first test suite

Say you have a string utility to test. Create `test_strings.cpp`:

```cpp
#include <string>

#include <ftest/ftest.hpp>

int main()
{
    ftest::Suite strings("string utils");

    strings.add("to_upper uppercases ascii", [] {
            ftest::require_equal(to_upper("abc"), std::string("ABC"),
                                 "basic uppercase round trip");
    });

    strings.add("empty input stays empty", [] {
            ftest::require_equal(to_upper(""), std::string(""),
                                 "empty edge case");
    });

    strings.add("rejects non-lowercase garbage by throwing", [] {
            ftest::require_throws<std::runtime_error>(
                    "invalid input must throw",
                    [] { to_upper("\xff\xfe"); });
    });

    ftest::Runner runner;
    runner.add(std::move(strings));
    return runner.run_all();
}
```

Build and run:

```sh
c++ -std=c++17 -Ivendor -pthread test_strings.cpp -o test_strings
./test_strings
```

```
== series: string utils (3 tests)
  - to_upper uppercases ascii [PASS]
  - empty input stays empty [PASS]
  - rejects non-lowercase garbage by throwing [PASS]

ALL 3 TESTS PASSED
```

What you get for free:

* **Exit code 0/1** — wire straight into CI or a Makefile.
* **Failures don't stop the run** — each case is isolated in its own
  try/catch.
* **No macros, no registration** — tests are lambdas; the description string
  doubles as documentation.

A failure looks like this:

```
  - basic uppercase round trip [FAIL]
my check
       expected: [ABC]
       got     : [aBC]
```

Group related cases into multiple Suites and hand them all to one Runner;
each becomes a titled section of the output.

---

## 4. Assertions — choosing the right one

All assertions live in `namespace ftest`, take a human-readable `what`
label as the last argument, and throw `TestFailure`.

| You want to check… | Use |
|---|---|
| any condition | `require(cond, "what")` |
| something did NOT happen | `require_false(cond, "what")` |
| values are identical | `require_equal(got, expected, "what")` — works on strings *and* integers, prints both values on mismatch |
| output contains something | `require_contains(haystack, needle, "what")` |
| a call throws | `require_throws<ExpectedT>("what", fn)` |
| a call escapes cleanly | `require_no_throw("what", fn)` |

Tips:

* Write `what` as a **specification**, not a description:
  `"timeout kills a hung child"` reads better in a report than `"test 7"`.
* Prefer `require_equal(got, expected, ...)` over
  `require(got == expected, ...)` — the former shows you *both values*
  when it fails.
* Integer overloads accept any width (they normalize through `long long`),
  so mixing `size_t` and `int` won't bite.

For process results specifically, skip manual WIFEXITED archaeology and use
the matchers from section 6.

---

## 5. Leak hunting: fds, heap, threads

Three guards share one pattern: **snapshot at construction, diff at
`require_clean()`, name the culprits on failure.**

### File descriptors — `FdLeakGuard`

```cpp
suite.add("hashing session closes every descriptor", [] {
        ftest::FdLeakGuard guard("hashing session");
        hash_file("/tmp/big.bin");          // opens, forgets to close?
        guard.require_clean();              // FAIL: "2 file descriptor(s)
});                                          //  left open: 7, 8"
```

Only descriptors *newly opened* during the guarded scope count — stdin,
your harness's own fds etc. are baseline. Descriptors you closed again are
fine. Needs `/proc/self/fd` (Linux).

### Heap growth — `AllocGuard`

```cpp
if (ftest::alloc_tracking_available())      // false under ASan
{
    ftest::AllocGuard guard("parsing 200 KB");
    parse(big_input);
    guard.require_no_heap_growth();         // or pass a byte tolerance
}
```

Works via glibc's `mallinfo2`: catches forgotten `free()`s and unbalanced
`operator new`. Two caveats handled for you: under AddressSanitizer it
disables itself (LSan covers leaks there), and caching allocators can justify
a tolerance — `require_no_heap_growth(4096)` allows ≤ 4 KB drift.

### Threads — `ThreadLeakGuard` / `StuckThreadGuard`

`ThreadLeakGuard` answers *"did I join everything I created?"* — a thread
that exited but was never joined still counts as a leak (joining IS clean
teardown):

```cpp
suite.add("worker pool joins all workers", [] {
        ftest::ThreadLeakGuard guard("worker pool");
        run_pool_and_join();                // forgot one pthread_join?
        guard.require_clean();              // FAIL: "thread(s) not joined"
});
```

`StuckThreadGuard` answers the harder question — *"did my workers actually
FINISH within the deadline?"* — and names the stuck ones:

```cpp
pthread_t worker;
pthread_create(&worker, nullptr, run_job, &job);

ftest::StuckThreadGuard guard("pool");
guard.watch(worker, "compressor");

guard.require_all_finished(std::chrono::milliseconds{2000});
// OK            → guard joined it; never join/detach that handle again
// throws        → message names "compressor"; YOU own cleanup now:
//                  pthread_join(worker, ...) or pthread_detach(worker)
```

Ownership rules differ between build modes (timedjoin vs POSIX-fallback) —
see [reference.md](reference.md), StuckThreadGuard. In tests, spawn raw
`pthread_create` threads; do **not** pair this guard with `std::thread`
(its destructor would terminate the process on a handle the guard already
reaped).

---

## 6. Testing binaries that crash or hang

Anything that can segfault, deadlock or block forever goes through the
subprocess runner — **never called in-process**, or one bad case kills your
whole test binary.

### The simple way: `CommandRunner`

```cpp
ftest::CommandRunner app("./mytool");

ftest::RunResult r = app.run({"--verbose", "file.txt"},   // argv[1..]
                             "optional stdin payload",    // piped to stdin
                             5000);                       // timeout ms
```

### The full way: `Subprocess`

```cpp
ftl::RunResult r = ftl::Subprocess("./mytool")
        .args({"--mode", "batch"})
        .stdin_data(large_payload)
        .env("MYTOOL_DEBUG", "1")             // added to inherited env
        .env("PATH", "/usr/bin:/bin", true)   // replace=true: ONLY these
        .timeout(std::chrono::seconds{30})
        .rlimit_nofile(16)                    // simulate fd exhaustion
        .split_output()                       // stdout ≠ stderr
        .run();

r.output;        // stdout only (split mode)
r.error_output;  // stderr only (split mode)
r.exit_code(); r.signaled(); r.signal_number(); r.timed_out();
```

Guarantees worth knowing:

* **A hanging child cannot hang your tests.** At the deadline the child gets
  SIGKILL and is reaped; `run()` returns with `timed_out == true`.
* Output is collected through poll loops that retry on EINTR; big payloads
  exercise pipe backpressure safely.
* A missing binary surfaces as exit code 127, not as a crash of your tester.

### Matchers — assert on results like a spec

```cpp
using namespace ftest;

require_exit(r, 0, "tool accepts valid input");         // exit 0 AND clean exit
require_signaled(crash_r, SIGSEGV, "corrupt input crashes hard");
require_timed_out(hang_r, "infinite loop is killed");
require_output_contains(r, "processed 42 items", "progress is reported");

describe_exit(r)   // "exit code 3" / "crashed: killed by signal SIGSEGV"
```

Typical black-box suite shape:

```cpp
Suite cli("CLI behavior");
cli.add("--help exits 0 and prints usage", [&] {
        require_output_contains(app.run({"--help"}), "usage", "--help text");
});
cli.add("missing file exits 2 with a message", [&] {
        RunResult r = app.run({"no-such-file.txt"});
        require_exit(r, 2, "missing file exit code");
        require_output_contains(r, "no-such-file.txt", "error names the file");
});
cli.add("SIGKILLed mid-run leaves no partial output", [&] { /* … */ });
```

---

## 7. ftl in production code

Nothing in `ftl/` knows about tests. Typical production uses:

### Scoped cleanup without try/catch noise

```cpp
void update_config()
{
        int fd = ::open(path, O_WRONLY);
        if (fd < 0)
                return;
        ftl::UniqueFd guard(fd);              // closed on every exit path

        auto rollback = ftl::make_scope_guard([&] {
                write_backup();               // runs unless dismissed
        });
        write_new_config(fd);
        rollback.dismiss();                   // success → keep new config
}
```

### Running tools with bounded time

```cpp
ftl::RunResult r = ftl::Subprocess("/usr/bin/gpg")
        .args({"--decrypt", archive})
        .stdin_data(passphrase)
        .env("GNUPGHOME", home + "/.gnupg")
        .timeout(std::chrono::seconds{60})
        .split_output()
        .run();

if (!r.exited() || r.exit_code() != 0)
        log_error("decrypt failed: " + ftl::describe_exit(r));
```

### Scratch space that can't leak

```cpp
{
        ftl::TempDir scratch("render_");
        render_to(scratch.path() + "/out.png");   // even on exceptions…
}                                                  // …tree is gone here
```

### Watching background workers

```cpp
std::vector<pthread_t> pool = start_pool(4);
{
        ftl::StuckThreadGuard monitor("shutdown");
        for (size_t i = 0; i < pool.size(); ++i)
                monitor.watch(pool[i], "worker-" + std::to_string(i));

        stop_accepting_work();

        try {
                monitor.require_all_finished(std::chrono::seconds{5});
        } catch (const std::exception& e) {
                log_error(e.what());   // names exactly which workers hung
        }
}
// timedjoin build: finished handles are joined; stuck ones are yours to
// detach/join (see reference).
```

Error style: hard failures (fork/pipe/poll errors) throw
`std::runtime_error` with errno text; child-level failures are data in
`RunResult` — they're the program's outcome, not the runner's.

---

## 8. Using ftl from plain C

`ftl/ftl.h` is self-contained C99 — no C++ compiler needed:

```c
/* _POSIX_C_SOURCE >= 200809L must be defined BEFORE any system include. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ftl/ftl.h"

int main(void)
{
        const char* args[] = {"-c", "echo hello", NULL};

        ftl_run_result r;
        if (ftl_run_command("/bin/sh", args, NULL, 5000, &r) != 0)
                return 1;                       /* setup failed; errno set */

        printf("exit=%d out=%s\n",
               WIFEXITED(r.status) ? WEXITSTATUS(r.status) : -1,
               r.output ? r.output : "(none)");
        ftl_free_run_result(&r);

        char* tmp = ftl_temp_file_create("app", "seed content");
        /* ... use tmp ... */
        ftl_temp_file_destroy(tmp);
        return 0;
}
```

Compile with `cc -std=c11 main.c` — nothing to link. Same guarantees as the
C++ runner: timeouts kill hung children, EINTR-safe loops throughout.

---

## 9. Recipes

### Determinism check (same input → same output)

```cpp
suite.add("digest is deterministic across 20 runs", [&] {
        std::string first;
        for (int i = 0; i < 20; ++i) {
                RunResult r = app.run({"-q", "-s", payload});
                require_exit(r, 0, "run succeeds");
                if (i == 0) first = r.output;
                require_equal(r.output, first, "identical digest every time");
        }
});
```

### Resource exhaustion

```cpp
suite.add("tool degrades gracefully when fds run out", [&] {
        RunResult r = app.run({"process"}, "", 10000, /*rlimit_nofile*/16);
        require(!r.signaled(), "no crash under RLIMIT_NOFILE=16");
});
```

### Capturing stdout of legacy printf-style code

```cpp
suite.add("banner matches the version string", [] {
        std::string printed = ftest::capture_stdout([] {
                print_banner();                    // old printf-based API
        });
        require_contains(printed, VERSION, "banner shows the version");
});
```

### Negative testing the guards themselves

```cpp
suite.add("leaked fd is detected", [] {
        FdLeakGuard guard("leak probe");
        int fd = open("/dev/null", O_RDONLY);
        require_throws<TestFailure>("leak must be reported",
                                    [&] { guard.require_clean(); });
        close(fd);
        guard.require_clean();                     // clean again afterwards
});
```

Every feature of the library has such a negative test in
`test_library.cpp` — when in doubt about semantics, read how the library
tests itself.

---

## 10. Philosophy & rules of thumb

1. **One behavior per test.** The description should read as a spec line.
2. **Crashy things live in subprocesses; logic lives in-process.**
3. **Wrap resource-heavy scopes in guards** — they're cheap when green and
   priceless when red.
4. **RAII everywhere**: if it opens, it closes; if it creates, it removes.
5. **Prefer matcher precision** — `require_exit` tells you *what went wrong*
   ("process did not exit normally: crashed: killed by signal SIGSEGV"),
   raw status comparisons don't.
6. **No global state, no static constructors, no macros** — suites compose
   freely and run deterministically.
7. **Portability is graceful degradation**, not lowest-common-denominator:
   features shrink on non-Linux platforms, they never break the build.

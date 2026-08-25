# ftest / ftl — low-level C/C++ utilities + testing framework

Two header-only libraries for C and C++ projects on Linux. **New here? Start
with the [guide](docs/guide.md)** — a tutorial-style introduction covering
your first suite, leak hunting, subprocess testing, and production use of
ftl. Full per-symbol details live in
[docs/reference.md](docs/reference.md).

* **`ftl/`** — general-purpose low-level utilities usable in production code:
  RAII descriptors, pipes, stdout capture, temp files/dirs, a builder-style
  subprocess runner, thread tracking, allocation snapshots, scope guards,
  plus a pure-C API (`ftl/ftl.h`).
* **`ftest/`** — a small testing framework built **on top of** ftl: suites,
  colored output, assertions, leak guards (fd / heap / threads), process
  matchers.

```
├── ftl/                    layer 1: production-safe utilities (C++17)
│   ├── ftl.h               pure-C99 API (subprocess run, temp files)
│   ├── ftl.hpp             umbrella include
│   ├── core/               scope_guard, error helpers, UniqueFd + fd_snapshot
│   ├── io/                 pipe, capture (any fd), TempFile/TempDir
│   ├── process/            Subprocess builder runner
│   ├── thread/             /proc/self/task snapshot, StuckThreadGuard
│   └── alloc/              mallinfo2 snapshots, ASan detection
├── ftest/                  layer 2: test framework (depends on ftl)
│   ├── framework.hpp       colors, TestFailure, require_* assertions, Suite, Runner
│   ├── guards.hpp          ThreadLeakGuard, StuckThreadGuard wrapper
│   ├── matchers.hpp        require_exit / require_signaled / require_timed_out …
│   └── ftest.hpp           umbrella include
├── c_smoke.c               pure-C smoke test of ftl.h
├── test_library.cpp        ftest self-test (the library tests itself)
├── test_unit.cpp           in-process tester of the parent project (if embedded)
└── test_cli.cpp            black-box CLI tester of the parent project (if embedded)
```

## Quick start (testing)

```cpp
#include <ftest/ftest.hpp>

int main()
{
    ftest::Suite suite("my feature");

    suite.add("two plus two is four", [] {
            ftest::require(2 + 2 == 4, "math is broken");
    });
    suite.add("binary exits cleanly", [] {
            ftest::require_exit(
                    ftest::Subprocess("./app").args({"--check"}).run(),
                    0, "app --check");
    });

    ftest::Runner runner;
    runner.add(std::move(suite));
    return runner.run_all();          // exit code 0 = all green
}
```

```sh
c++ -std=c++17 -I. -pthread my_tests.cpp -o my_tests && ./my_tests
```

Or with CMake in a bigger project:

```cmake
add_subdirectory(ftest)              # or FetchContent / installed export
target_link_libraries(my_tests PRIVATE ftest::ftest)
```

## Quick start (production use of ftl)

```cpp
#include <ftl/ftl.hpp>

void demo()
{
    // subprocess with timeout, env override, split streams
    ftl::RunResult r = ftl::Subprocess("/usr/bin/tool")
                               .arg("--fast")
                               .stdin_data("payload")
                               .env("TOOL_MODE", "batch")
                               .timeout(std::chrono::seconds{10})
                               .split_output()
                               .run();
    if (!r.exited() || r.exit_code() != 0)
            throw std::runtime_error(ftl::describe_exit(r));

    // RAII fd + scope guard
    {
            ftl::UniqueFd fd = ftl::open_fd("/etc/config", O_RDONLY);
            auto on_exit = ftl::make_scope_guard([] { /* cleanup */ });
    }   // everything released here, even on exceptions

    // temp files removed recursively on scope exit
    ftl::TempDir scratch("build_");
}
```

Pure C:

```c
#include "ftl/ftl.h"

/* Define _POSIX_C_SOURCE=200809L before any system header. */
const char* args[] = {"-c", "echo hello", NULL};
ftl_run_result r;
if (ftl_run_command("/bin/sh", args, NULL, 5000, &r) == 0) {
        puts(r.output ? r.output : "(no output)");
        ftl_free_run_result(&r);
}
```

Compile: `cc -std=c11 -D_POSIX_C_SOURCE=200809L main.c` — nothing else to link.

## The testing toolkit

### Assertions (`framework.hpp`)
All throw `ftest::TestFailure`; the Suite loop catches it and keeps going.

```cpp
require(cond, what);  require_false(cond, what);
require_equal(got, expected, what);         // string + numeric overloads
require_contains(haystack, needle, what);
require_throws<T>(what, fn);  require_no_throw(what, fn);
```

### Process matchers (`matchers.hpp`)
```cpp
require_exit(result, 0, "what");            // also rejects timeouts/signals
require_signaled(result, SIGSEGV, "what");  // expected crash
require_timed_out(result, "what");          // expected hang
require_output_contains(result, "needle", "what");
```

### Leak guards
```cpp
{   ftest::FdLeakGuard g("feature x");      // diffs /proc/self/fd
    exercise_feature();
    g.require_clean();                      // throws listing leaked fds
}

{   ftest::AllocGuard g("parsing 200 KB");  // glibc mallinfo2 diff;
    parse(big_input);                       // no-op under ASan
    g.require_no_heap_growth(/*tolerance*/ 4096);
}

{   ftest::ThreadLeakGuard g("worker pool"); // diffs /proc/self/task;
    spawn_and_join_workers();                // unjoined threads count as leaks
    g.require_clean();
}
```

### Stuck-thread detection (`guards.hpp`)
Works on raw pthread handles (see ownership rules in
`ftl/thread/thread_count.hpp`):

```cpp
pthread_t worker;
pthread_create(&worker, nullptr, run_job, &job);

ftest::StuckThreadGuard guard("pool");
guard.watch(worker, "worker");

guard.require_all_finished(std::chrono::milliseconds{2000});
// returns normally → guard joined every watched thread, do nothing.
// throws → message names each stuck thread; you own cleanup
//          (pthread_join or pthread_detach).
```

### Stdout capture (`capture.hpp`)
```cpp
{
    ftest::OutputCapture capture(STDOUT_FILENO, stdout);
    printf("captured\n");                    // printf/write/stdcout all land here
    std::string text = capture.content();    // "captured\n"
}
```
Generalized in ftl: capture any descriptor via `ftl::FdCapture(fd)`.

### Subprocess runner (`process.hpp` / `ftl/process/subprocess.hpp`)
```cpp
ftest::CommandRunner app("./tool");          // legacy simple API
ftest::RunResult r = app.run({"-q", "-s", "abc"}, "stdin", 5000, /*nofile*/16);
r.output; r.status; r.timed_out;

ftl::Subprocess("./tool")                    // modern builder API
        .args({"-q"}) .stdin_data("...") .env("K","v",/*replace*/false)
        .timeout(std::chrono::milliseconds{5000})
        .rlimit_nofile(16) .split_output()
        .run() → RunResult{output, error_output, exit_code(), signal_number(),
                           timed_out, describe_exit()}
```
A hanging child is SIGKILLed at the deadline and reaped — `run()` always
returns. Output collection is poll-driven with EINTR-safe loops.

### Design notes
* **Header-only**, C++17 core, C99-compatible C API. No build integration
  beyond an include path (or link `ftest::ftest` / `ftl::ftl` in CMake).
* **Exceptions as control flow in tests**: failures unwind into the Suite
  loop, so all RAII helpers clean up even when an assertion throws.
* **No registration macros** — plain lambdas via `suite.add(desc, fn)`.
* **Process isolation where crashes live**: anything that might crash or hang
  goes through the subprocess runner.
* `ftl/` never includes `ftest/` headers; `ftest/` depends only on `ftl/`.

### Limitations
* fd/thread tracking needs Linux (`/proc`).
* Allocation tracking needs glibc (`mallinfo2`) and disables itself under
  AddressSanitizer (LSan covers leaks there instead).
* Colors are always emitted.

### Portability

Everything compiles as strict POSIX where possible; platform-specific
features degrade gracefully instead of failing:

| Feature | Linux/glibc | Other POSIX (macOS, BSD, musl) |
|---|---|---|
| Subprocess, pipes, capture, temp files/dirs | full | **full** (strict POSIX) |
| `env()` overrides | `execvpe` | built-in PATH-searching `execve` fallback — same behavior |
| `fd_snapshot()`, `FdLeakGuard`, `ThreadLeakGuard` | works | snapshots come back empty → guards pass trivially |
| `StuckThreadGuard` | reaps finished threads via `pthread_timedjoin_np` | liveness probes via `pthread_kill(handle, 0)`; never reaps — you join/detach every watched handle yourself |
| `AllocGuard` / heap tracking | glibc `mallinfo2` (off under ASan) | disabled |

Full per-symbol details: [docs/reference.md](docs/reference.md).

## Build & self-test

```sh
make selfcheck   # build + run the library self-test (17 tests, ASan+UBSan)
make c-smoke     # pure-C API smoke test
make project     # parent-project unit/CLI testers (only when embedded)
make clean
```

or `cmake -S . -B build && cmake --build build && ctest --test-dir build`.

See `AGENTS.md` for contributor/agent conventions and `TESTING.md` for how the
parent-project testers are organized. Tutorials in
[`docs/guide.md`](docs/guide.md), full per-symbol documentation in
[`docs/reference.md`](docs/reference.md).

# Tests

This directory contains the reusable test framework (`ftest/`), its
production-grade substrate (`ftl/` — see `README.md` first for assertions,
guards and the process runner), and the parent project's testers.

```
├── ftl/  ftest/        the libraries themselves
├── c_smoke.c           pure-C API smoke test
├── test_library.cpp    ftest self-test: the library tests itself
├── test_unit.cpp       in-process tests (links real object files; embedded only)
└── test_cli.cpp        black-box tests (spawns the CLI binary; embedded only)
```

## Running

```sh
make selfcheck   # library self-test (always available)
make c-smoke     # pure-C API smoke test
make portable    # strict-POSIX fallback probe (feature macros disabled)
make project     # unit + CLI series — needs ../includes and ../obj, i.e. this
                 # dir must live inside its parent project
make clean
```

Exit code is `0` only when every test passes, so CI can use it directly.

## What each tester covers

### test_library.cpp — the library tests itself

Every ftest/ftl feature has a positive *and* negative test proving the
detectors detect:

| Series          | Coverage |
|-----------------|----------|
| Assertions      | failure messages carry expected/got/label |
| fd tracking     | leaked descriptor detected; clean scopes pass; RAII closes on scope exit |
| heap tracking   | stack-only scope clean; 4 KB malloc flagged |
| capture & files | stdout round trip + restore; TempFile/TempDir lifecycle |
| process runner  | output verbatim, exit codes, missing binary → 127, timeout kills hangs, stdin piping |
| subprocess v2   | split_output separates streams; env override inherited vs replacing; signal death reported via matchers |
| thread tracking | unjoined thread flagged as leak; stuck worker named by StuckThreadGuard; finished workers pass |

### test_unit.cpp — in-process (fast, precise)

Links against your real object files **without** `main.o` and calls functions
directly. Stdout is captured with `OutputCapture`, so digests are compared
exactly. Covers reference vectors (RFC/NIST), file-descriptor mode, helpers,
determinism, plus `FdLeakGuard` / `AllocGuard` resource checks around real
workloads.

### test_cli.cpp — black-box (crash-safe)

Spawns the CLI via `CommandRunner`. This is where segfaults, hangs and
argument-parsing bugs surface without killing the test binary. Covers crash
regressions, error reporting/exit codes, golden outputs, RLIMIT_NOFILE runs,
pipe backpressure and odd input files.

## Adding tests

Pick the tester that matches the level:

* **Pure function behavior** → `test_unit.cpp`, call it directly.
* **Argument handling, output formatting, crashes, resource limits** →
  `test_cli.cpp`, spawn the binary.
* **New library capability** → `test_library.cpp`, both directions
  (works / is detected when violated).

Then add a case to the matching suite:

```cpp
suite.add("short description of the behavior", [&] {
        ftest::require_equal(actual, expected, "what was checked");
});
```

Guidelines used throughout:

1. One behavior per case; the description should be readable as a spec line.
2. Prefer golden/reference values over re-implementing the algorithm.
3. Wrap anything that opens resources in `FdLeakGuard` /
   `AllocGuard::require_no_heap_growth()` / `ThreadLeakGuard`.
4. Anything that could crash or hang goes through the subprocess runner,
   never called in-process.
5. Threads in tests are raw `pthread_create` + explicit join/detach.

## Output format

```
== series: Suite name (N tests)
  - description [PASS]
  - failing description [FAIL]
       expected: [...]
       got     : [...]

ALL N TESTS PASSED
```

# Tests

This folder contains the project's testers. They are built on the reusable
library in [`../ftest/`](../ftest/README.md) — read that first for assertions,
guards and the process runner.

```
tests/
├── test_unit.cpp   in-process tests: links the real md5.o/sha256.o/helpers.o
└── test_cli.cpp    black-box tests: runs ./ft_ssl as a subprocess
```

## Running

```sh
make test        # builds and runs unit + CLI series
make test-lib    # optional: self-check of the ftest library itself
make debug       # rebuild ./ft_ssl with ASan/UBSan, then:
make test        # the same suite runs under sanitizers (the test binaries
                 # are always sanitizer-linked, so this just works)
```

Exit code is `0` only when every test passes, so CI can use it directly.

## What each tester covers

### test_unit.cpp — in-process (fast, precise)

Links against your real object files **without** `main.o` and calls
`md5()` / `sha256()` / helpers directly. Stdout is captured with
`ftest::OutputCapture`, so digests are compared exactly.

| Series                     | Coverage |
|----------------------------|----------|
| MD5 reference vectors      | empty string, RFC vectors, block-boundary lengths 55/56/63/64/65/119/120/1000 (every padding edge), binary pattern, 1 MB stress |
| SHA256 reference vectors   | same shape + NIST multi-block vector |
| File descriptor mode       | file input path vs string path agreement, binary content through buffering |
| Helpers                    | ft_strncmp semantics (n=0, partial, unsigned) and ft_putstr_fd |
| Resource & leak checks     | FdLeakGuard over real hashing sessions, AllocGuard on 200 KB input, /dev/null, directory fd read-error path, determinism, capture restore |

Reference values are official test vectors (RFC 1321, NIST) or generated with
an independent implementation.

### test_cli.cpp — black-box (crash-safe)

Spawns `./ft_ssl` via `ftest::CommandRunner`. This is where segfaults,
hangs and argument-parsing bugs surface without killing the test binary.

| Series                       | Coverage |
|------------------------------|----------|
| Crash regression             | no args, flags-only, `-s` missing arg, empty/over-long command names |
| Error reporting              | usage line, invalid-command banner, missing-file message, exit code 2 |
| Golden output                | exact stdout for `-s`, `-r`, `-p`, `-q`, `-qp` combinations against known digests |
| Fd & resource stress         | RLIMIT_NOFILE=16 runs, 20x determinism, 200 KB piped stdin (pipe backpressure), 1 MB file, directory/empty/unreadable files |

## Adding tests

Pick the tester that matches the level:

* **Pure function behavior** → `test_unit.cpp`, call it directly.
* **Argument handling, output formatting, crashes, resource limits** →
  `test_cli.cpp`, spawn the binary.

Then add a case to the matching suite (or create one):

```cpp
suite.add("short description of the behavior", [&] {
        ftest::require_equal(actual, expected, "what was checked");
});
```

Guidelines used throughout:

1. One behavior per case; the description should be readable as a spec line.
2. Prefer golden/reference values over re-implementing the algorithm.
3. Wrap anything that opens resources in `FdLeakGuard` /
   `AllocGuard::require_no_heap_growth()`.
4. Anything that could crash or hang goes through `CommandRunner`, never
   called in-process.
5. New boundary lengths or flag combinations: extend the tables at the top of
   the file rather than copy-pasting cases.

## Output format

```
== series: Suite name (N tests)
  - description [PASS]
  - failing description [FAIL]
       expected: [...]
       got     : [...]

ALL 31 TESTS PASSED
```

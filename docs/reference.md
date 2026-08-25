# ftl / ftest API Reference

One entry per public symbol. Conventions across the whole library:

* **Namespaces**: C++ symbols live in `ftl` (production) and `ftest` (testing).
* **Errors**: C++ functions throw `std::runtime_error` on hard failures
  (errno included); test assertions throw `ftest::TestFailure`. The C API
  returns `-1` with errno set, never throws.
* **RAII**: everything owning an OS resource releases it in its destructor,
  even when an exception unwinds through the scope.
* **Portability tags**: **[Linux]** needs procfs, **[glibc]** needs glibc or
  musl, unmarked symbols are strict POSIX. Reduced-semantics fallbacks are
  noted per symbol.

---

## ftl/core/scope_guard.hpp

### `template <typename Fn> class ftl::ScopeGuard`
Runs `fn()` exactly once at scope exit unless dismissed.

| Member | Semantics |
|---|---|
| `ScopeGuard(Fn fn)` | arms the guard |
| `dismiss() noexcept` | cancels the pending call |
| destructor | calls `fn()` if still armed |

Move-only; move-assignment is deleted to avoid clobbering armed guards.

### `template <typename Fn> ScopeGuard<Fn> ftl::make_scope_guard(Fn&& fn)`
Deduces `Fn`:

```cpp
auto g = ftl::make_scope_guard([&] { cleanup(); });
```

## ftl/core/error.hpp

### `[[noreturn]] void ftl::throw_errno(const std::string& what)`
Throws `std::runtime_error(what + ": " + strerror(errno))`.

## ftl/core/fd.hpp

### `class ftl::UniqueFd`
Owning descriptor wrapper (default `-1`).

| Member | Semantics |
|---|---|
| `UniqueFd(int fd)` | takes ownership |
| `int get() const` | borrows the fd |
| `int release() noexcept` | gives up ownership, returns the fd |
| `void reset(int fd = -1) noexcept` | closes current, takes new |
| `explicit operator bool()` | true when holding an open fd |

Move-only; closes on destruction/reset.

### `UniqueFd ftl::open_fd(const std::string& path, int flags = O_RDONLY)`
Throws on failure.

### `std::vector<int> ftl::fd_snapshot()` **[Linux]**
Sorted list of all open descriptors from `/proc/self/fd`; empty vector when
procfs is unavailable.

## ftl/io/pipe.hpp

### `struct ftl::Pipe { UniqueFd read_fd, write_fd; }`
* `static Pipe create()` — throws on `pipe(2)` failure.

### `void ftl::write_all(int fd, const char* data, size_t size)` /
### `void ftl::write_all(int fd, const std::string& data)`
Writes everything, retrying on EINTR; stops silently on hard write errors.

## ftl/io/capture.hpp

### `class ftl::FdCapture` (alias `ftl::OutputCapture`)
Redirects an existing descriptor to a tmpfile for the object's lifetime;
restores it on destruction (exception-safe).

| Member | Semantics |
|---|---|
| `FdCapture(int target_fd, FILE* stream_to_flush = nullptr)` | pass `stdout` as second arg when capturing fd 1 so stdio buffers land in the capture |
| `std::string content()` | flushes the stream (if any), returns everything captured so far |

### `std::string ftl::capture_stdout(const std::function<void()>& fn)`
Runs `fn()` with stdout captured; returns its output.

## ftl/io/temp.hpp

### `class ftl::TempFile`
`TempFile(path, content)` writes and keeps the path; removes the file on
destruction. Non-copyable. `const std::string& path() const`.

### `class ftl::TempDir`
`TempDir(prefix)` creates `/tmp/prefix_XXXXXX` (mkdtemp); **recursively**
removes the tree on destruction. `const std::string& path() const`.

## ftl/process/subprocess.hpp

### `class ftl::RunResult`

| Member | Semantics |
|---|---|
| `int status` | raw waitpid status (use the WIFEXITED family) |
| `bool timed_out` | child was SIGKILLed at the deadline |
| `std::string output` | stdout, merged with stderr unless split |
| `std::string error_output` | stderr (only with `split_output()`) |
| `bool exited() / signaled()` | classification helpers |
| `int exit_code()` | WEXITSTATUS, or -1 if not exited normally |
| `int signal_number()` | WTERMSIG, or 0 |

### `std::string ftl::describe_exit(const RunResult&)`
Human-readable summary: `"exit code 3"`,
`"crashed: killed by signal SIGSEGV"`, `"timed out (possible hang)"`.

### `class ftl::Subprocess` — builder-style runner

```cpp
ftl::RunResult r = ftl::Subprocess("./tool")
        .args({"-q", "-s", "abc"})   // or .arg("x") one by one
        .stdin_data("payload")
        .env("KEY", "value")         // inherit parent env + override
        .env("PATH", "/bin", true)   // replace=true: ONLY these entries
        .timeout(std::chrono::seconds{10})   // or .timeout_ms(10000)
        .rlimit_nofile(64)           // applied in the child pre-exec
        .split_output()              // separate error_output buffer
        .run();
```

Guarantees:
* `run()` always returns — hanging children are SIGKILLed at the deadline,
  then reaped.
* Poll-driven output collection; all loops retry on EINTR.
* Child-side pipe fds are closed in the parent right after fork, so EOF
  arrives even when the child never reads stdin.
* exec failure surfaces as exit code 127.
* Env overrides resolve through the **child's** PATH: `execvpe`
  **[glibc]**, otherwise a built-in PATH-searching `execve_portable`.

## ftl/thread/thread_count.hpp

### `struct ftl::ThreadSnapshot { size_t count; }`
### `ThreadSnapshot ftl::thread_snapshot()` **[Linux]**
Live thread count via `/proc/self/task`; 0 when procfs is unavailable.

### `class ftl::StuckThreadGuard`

```cpp
pthread_t worker;
pthread_create(&worker, nullptr, run_job, &job);

ftl::StuckThreadGuard guard("pool");
guard.watch(worker, "worker");
guard.require_all_finished(std::chrono::milliseconds{2000});
```

| Behavior | timedjoin build **[glibc]** | POSIX-fallback build |
|---|---|---|
| detection | `pthread_timedjoin_np` slices | `pthread_kill(handle, 0)` probes every 10 ms |
| finished threads | **reaped by the guard** — never join/detach again | **never reaped** — caller joins/detaches every watched handle |
| stuck threads | named in the thrown message; untouched (caller owns cleanup) | same |

Throws `std::runtime_error` naming every stuck thread after the deadline.

## ftl/alloc/alloc_info.hpp

| Symbol | Notes |
|---|---|
| `bool running_under_asan()` | compile-time ASan detection (gcc + clang) |
| `struct AllocSnapshot { long allocated_bytes, mmapped_bytes; }` | **[glibc]** mallinfo2 snapshot; `{-1,-1}` elsewhere |
| `bool alloc_tracking_available()` | **[glibc]** true when not under ASan |
| `AllocSnapshot alloc_snapshot()` | **[glibc]**; no-op values elsewhere |

## ftl.h — pure-C API (C99, static inline)

Requires `_POSIX_C_SOURCE >= 200809L` **before any system include**.

### `typedef struct { int status; int timed_out; char* output; } ftl_run_result`
`status` is the waitpid status; `output` is malloc'd, NUL-terminated, merged
stdout+stderr (NULL if nothing produced or on OOM).

### `int ftl_run_command(const char* program, const char* const* args, const char* stdin_data, long long timeout_ms, ftl_run_result* result)`
Runs `program` with `args` (argv[1..], NULL-terminated, may be NULL), piping
`stdin_data` (may be NULL) to the child. SIGKILLs the child after
`timeout_ms`. Returns 0 and fills `*result` on success; -1 with errno set on
setup failure. Free with `ftl_free_run_result`.

### `void ftl_free_run_result(ftl_run_result*)`
Frees `output`.

### `char* ftl_temp_file_create(const char* prefix, const char* content)`
Creates `/tmp/prefix_XXXXXX` (mkstemp), writes `content`, returns a malloc'd
path or NULL with errno set.

### `void ftl_temp_file_destroy(char* path)`
Unlinks the file and frees the path string.

---

# ftest — testing layer

## ftest/framework.hpp

Colors (`RESET/RED/GREEN/YELLOW/BLUE/BOLD`), `TestFailure`, assertions,
`Suite`, `Runner`.

### Assertions (all throw `TestFailure`; failure messages show expected/got)

| Assertion | Checks |
|---|---|
| `require(cond, what)` | generic boolean |
| `require_false(cond, what)` | negation |
| `require_equal(got, expected, what)` | strings and integers (overloads; failure prints both values labeled) |
| `require_contains(haystack, needle, what)` | substring presence |
| `require_throws<T>(what, fn)` | `fn()` throws anything |
| `require_no_throw(what, fn)` | `fn()` escapes nothing |

### `class ftest::Suite`
`Suite(name)`, chainable `add(description, fn)`, move-only.
`run()` executes each case in its own try/catch: `TestFailure` → red FAIL,
`std::exception` → yellow ERROR, anything else → red ERROR. One failure
never stops the series.

### `class ftest::Runner`
`add(Suite)` (move), `run_all()` returns exit code 0/1 for CI.

## ftest/capture.hpp
Aliases over ftl: `OutputCapture`, `capture_stdout`.

## ftest/files.hpp
`FileDescriptor(path, flags = O_RDONLY)` (throws on open failure),
plus aliases `TempFile`, `TempDir`, `UniqueFd`.

## ftest/fdtrack.hpp

### `class ftest::FdLeakGuard`
```cpp
FdLeakGuard g("feature x");
exercise();
g.require_clean();   // throws listing leaked descriptors
```
**[Linux]** Diffs `fd_snapshot()` against the constructor baseline; only new
descriptors count.

## ftest/alloc.hpp
Aliases `running_under_asan`, `alloc_tracking_available`,
`alloc_snapshot`, `AllocSnapshot`, plus:

### `class ftest::AllocGuard`
`require_no_heap_growth(long tolerance_bytes = 0)` — no-op when tracking is
unavailable; otherwise compares mallinfo2 in-use + mmapped bytes against the
baseline **[glibc]**.

## ftest/process.hpp
Aliases `RunResult`, `Subprocess`, `describe_exit`, plus:

### `class ftest::CommandRunner` — legacy simple API
`CommandRunner(program).run(args, stdin_data = "", timeout_ms = 5000,
rlimit_nofile = 0)`. Thin wrapper over `Subprocess`; reusable across runs.

## ftest/guards.hpp

### `class ftest::ThreadLeakGuard` **[Linux]**
Baseline thread count at construction;
`require_clean(expected_new_threads = 0)` fails when more threads exist than
baseline + expected. Unjoined-but-exited threads still count — joining is
part of clean teardown.

### `class ftest::StuckThreadGuard`
Chrono-friendly wrapper over `ftl::StuckThreadGuard`:
`watch(pthread_t, label)`, `require_all_finished(duration)`.
Ownership rules follow the build mode (see reference above).

## ftest/matchers.hpp

| Matcher | Checks |
|---|---|
| `require_exit(result, code, what)` | child exited normally AND with that code (also rejects timeouts/signals, describing what happened instead) |
| `require_signaled(result, signal, what)` | crashed by signal; `signal > 0` also checks which one |
| `require_timed_out(result, what)` | deadline enforcement triggered |
| `require_output_contains(result, needle, what)` | substring of stdout(+stderr merged) |

---

See `README.md` for quick starts and `AGENTS.md` for contributor rules.



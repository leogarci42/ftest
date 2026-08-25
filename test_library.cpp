#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <ftest/ftest.hpp>

namespace {

using namespace ftest;

void test_require_failure_message()
{
        bool caught = false;
        try
        {
                require_equal(std::string("actual"), std::string("wanted"),
                              "message probe");
        }
        catch (const TestFailure& e)
        {
                caught = true;
                std::string message = e.what();
                require_contains(message, "wanted", "failure shows expected");
                require_contains(message, "actual", "failure shows got");
                require_contains(message, "message probe",
                                 "failure shows the assertion label");
        }
        require(caught, "require_equal must throw TestFailure on mismatch");
}

void test_fd_leak_guard_detects_leak()
{
        FdLeakGuard guard("intentional leak");
        int fd = open("/dev/null", O_RDONLY);
        require(fd >= 0, "setup: cannot open /dev/null");
        require_throws<TestFailure>("leaked fd must be reported", [&] {
                guard.require_clean();
        });
        close(fd);
        guard.require_clean();
}

void test_fd_leak_guard_passes_when_clean()
{
        FdLeakGuard guard("clean scope");
        {
                FileDescriptor fd("/dev/null");
                require(fd.get() >= 0, "setup: invalid descriptor");
        }
        guard.require_clean();
}

void test_alloc_guard_passes_without_allocation()
{
        if (!alloc_tracking_available())
                return;
        AllocGuard guard("stack-only scope");
        volatile int sink = 0;
        for (int i = 0; i < 100; ++i)
                sink += i;
        (void)sink;
        guard.require_no_heap_growth();
}

void test_alloc_guard_detects_heap_growth()
{
        if (!alloc_tracking_available())
                return;
        AllocGuard guard("heap growth probe");
        void* block = malloc(4096);
        require(block != nullptr, "setup: malloc failed");
        memset(block, 1, 4096);
        try
        {
                guard.require_no_heap_growth();
                free(block);
                throw TestFailure(
                        "allocation tracker did not notice a 4 KB malloc");
        }
        catch (const TestFailure& e)
        {
                free(block);
                std::string message = e.what();
                if (message.find("did not notice") != std::string::npos)
                        throw;
                require_contains(message, "heap grew",
                                 "allocation report names growth");
        }
}

void test_output_capture_round_trip()
{
        std::string captured = capture_stdout([] {
                printf("captured %d\n", 42);
        });
        require_equal(captured, std::string("captured 42\n"),
                      "capture_stdout returns what was printed");
        std::string restored = capture_stdout([] {
                printf("restored\n");
        });
        require_equal(restored, std::string("restored\n"),
                      "stdout is restored after a capture");
}

void test_temp_file_and_dir_lifecycle()
{
        std::string path;
        {
                TempFile file("/tmp/opencode/ftest_probe.bin", "payload");
                path = file.path();
                std::ifstream in(path);
                require(static_cast<bool>(in), "temp file must exist while in scope");
                std::string content((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
                require_equal(content, std::string("payload"),
                              "temp file content round trip");
        }
        std::ifstream after(path);
        require_false(static_cast<bool>(after),
                      "temp file must be removed out of scope");

        TempDir dir("ftest_probe");
        FileDescriptor probe(dir.path() + "/marker.txt",
                             O_WRONLY | O_CREAT | O_TRUNC);
        require(probe.get() >= 0, "temp dir must be writable");
}

void test_process_runner_basics()
{
        CommandRunner echo("/bin/sh");
        RunResult result = echo.run({"-c", "echo hello"});
        require(!result.timed_out && !WIFSIGNALED(result.status),
                "/bin/sh -c 'echo hello' must exit normally");
        require_equal(result.output, std::string("hello\n"),
                      "process output is collected verbatim");

        RunResult failed = echo.run({"-c", "exit 3"});
        require(WIFEXITED(failed.status) && WEXITSTATUS(failed.status) == 3,
                "child exit codes are reported accurately");

        RunResult missing = CommandRunner("/no/such/binary").run({});
        require(WIFEXITED(missing.status) && WEXITSTATUS(missing.status) == 127,
                "missing executable surfaces as exit 127 from exec fallback");
}

void test_process_runner_timeout_kills_hangs()
{
        CommandRunner shell("/bin/sh");
        auto start = std::chrono::steady_clock::now();
        RunResult result = shell.run({"-c", "sleep 30"}, "", 500);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
        require(result.timed_out, "hanging child must be flagged as timed out");
        require(elapsed < 3000,
                "timeout enforcement took too long: "
                        + std::to_string(elapsed) + " ms");
}

void test_process_runner_stdin_piping()
{
        CommandRunner shell("/bin/sh");
        RunResult result = shell.run({"-c", "cat"}, "pipe me through");
        require_equal(result.output, std::string("pipe me through"),
                      "stdin data reaches the child process");
}

void test_subprocess_v2_split_output()
{
        RunResult result = Subprocess("/bin/sh")
                                   .args({"-c", "echo out; echo err >&2"})
                                   .split_output()
                                   .run();
        require_exit(result, 0, "split-output child must succeed");
        require_equal(result.output, std::string("out\n"),
                      "stdout goes to output when split");
        require_equal(result.error_output, std::string("err\n"),
                      "stderr goes to error_output when split");

        RunResult merged = Subprocess("/bin/sh")
                                   .args({"-c", "echo out; echo err >&2"})
                                   .run();
        require(merged.output.find("out") != std::string::npos
                        && merged.output.find("err") != std::string::npos,
                "merged mode combines stdout and stderr");
}

void test_subprocess_env_control()
{
        RunResult inherited = Subprocess("/bin/sh")
                                      .args({"-c", "echo $FTL_PROBE"})
                                      .env("FTL_PROBE", "injected")
                                      .run();
        require_output_contains(inherited, "injected",
                                "env override is visible to the child");

        setenv("FTL_PROBE", "parent-value", 1);
        RunResult replaced = Subprocess("/bin/sh")
                                     .args({"-c", "echo $HOME$FTL_PROBE"})
                                     .env("FTL_PROBE", "only-me", true)
                                     .run();
        require_output_contains(replaced, "only-me",
                                "replacing env keeps the override");
        require_false(replaced.output.find("parent-value")
                              != std::string::npos,
                      "replacing env drops parent variables");
        unsetenv("FTL_PROBE");
}

void test_subprocess_helpers_and_crash_report()
{
        RunResult crash =
                Subprocess("/bin/sh").args({"-c", "kill -SEGV $$"}).run();
        require_signaled(crash, SIGSEGV,
                         "self-SEGV child is reported as a signal death");
        require(crash.exit_code() == -1,
                "signaled child has no meaningful exit code");
}

void test_thread_leak_guard_detects_unjoined_threads()
{
        ThreadLeakGuard guard("thread leak probe");
        pthread_t worker;
        // The worker parks until released: a pthread that exits before the
        // guard samples /proc/self/task can already be reaped by the kernel
        // (non-leader threads never linger as zombies), so the probe must
        // hold it live-and-unjoined for the leak to be observable.
        std::atomic<bool> release_worker{false};
        auto body = +[](void* arg) -> void*
        {
                auto* flag = static_cast<std::atomic<bool>*>(arg);
                while (!flag->load(std::memory_order_acquire))
                        std::this_thread::sleep_for(
                                std::chrono::milliseconds{1});
                return nullptr;
        };
        require(pthread_create(&worker, nullptr, body, &release_worker) == 0,
                "setup: cannot spawn worker");
        bool detected = false;
        try
        {
                guard.require_clean();
        }
        catch (const TestFailure& e)
        {
                detected = true;
                std::string message = e.what();
                require_contains(message, "thread(s) not joined",
                                 "thread report names the problem");
        }
        require(detected, "unjoined thread must be flagged as a leak");
        release_worker.store(true, std::memory_order_release);
        pthread_join(worker, nullptr);
        guard.require_clean();
}

void* sleeping_worker(void*)
{
        sleep(30);
        return nullptr;
}

void* noop_worker(void*)
{
        return nullptr;
}

void test_stuck_thread_guard_names_stuck_workers()
{
        StuckThreadGuard guard("stuck probe");
        pthread_t stuck_thread;
        pthread_t quick_thread;
        require(pthread_create(&stuck_thread, nullptr, sleeping_worker,
                               nullptr) == 0,
                "setup: cannot spawn stuck worker");
        require(pthread_create(&quick_thread, nullptr, noop_worker, nullptr)
                        == 0,
                "setup: cannot spawn quick worker");
        guard.watch(stuck_thread, "stuck-worker");
        guard.watch(quick_thread, "quick-worker");

        bool detected = false;
        try
        {
                guard.require_all_finished(std::chrono::milliseconds{300});
        }
        catch (const std::exception& e)
        {
                detected = true;
                std::string message = e.what();
                require_contains(message, "stuck-worker",
                                 "report names the stuck thread");
        }
        require(detected, "a sleeping worker must be flagged as stuck");

        pthread_detach(stuck_thread);
}

void test_stuck_thread_guard_passes_when_threads_finish()
{
        StuckThreadGuard guard("finishing probe");
        pthread_t worker;
        require(pthread_create(&worker, nullptr, noop_worker, nullptr) == 0,
                "setup: cannot spawn worker");
        guard.watch(worker, "worker");
        guard.require_all_finished(std::chrono::milliseconds{2000});
}

} // namespace

int main()
{
        ::mkdir("/tmp/opencode", 0755);

        Suite assertions("Assertions");
        assertions.add("require_equal failure message carries full context",
                       test_require_failure_message);

        Suite fds("fd tracking");
        fds.add("leaked descriptor is detected and reported",
                test_fd_leak_guard_detects_leak);
        fds.add("closed descriptors pass the guard",
                test_fd_leak_guard_passes_when_clean);
        fds.add("FileDescriptor RAII closes on scope exit",
                test_fd_leak_guard_passes_when_clean);

        Suite heap("heap tracking");
        heap.add("stack-only scope shows no heap growth",
                 test_alloc_guard_passes_without_allocation);
        heap.add("4 KB malloc is detected as heap growth",
                 test_alloc_guard_detects_heap_growth);

        Suite tools("capture & files");
        tools.add("stdout capture round trip and restore",
                  test_output_capture_round_trip);
        tools.add("TempFile/TempDir lifecycle (create, read, cleanup)",
                  test_temp_file_and_dir_lifecycle);

        Suite process("process runner");
        process.add("output, exit codes and missing binaries are reported",
                    test_process_runner_basics);
        process.add("hanging child is killed by the timeout",
                    test_process_runner_timeout_kills_hangs);
        process.add("stdin piping reaches the child",
                    test_process_runner_stdin_piping);

        Suite subprocess_v2("subprocess v2");
        subprocess_v2.add("split_output separates stdout from stderr",
                         test_subprocess_v2_split_output);
        subprocess_v2.add("environment overrides are inherited or replacing",
                         test_subprocess_env_control);
        subprocess_v2.add("signal death is reported through the helpers",
                         test_subprocess_helpers_and_crash_report);

        Suite threads("thread tracking");
        threads.add("unjoined thread is detected as a leak",
                    test_thread_leak_guard_detects_unjoined_threads);
        threads.add("stuck worker is named by StuckThreadGuard",
                    test_stuck_thread_guard_names_stuck_workers);
        threads.add("finished workers pass StuckThreadGuard",
                    test_stuck_thread_guard_passes_when_threads_finish);

        Runner runner;
        runner.add(std::move(assertions));
        runner.add(std::move(fds));
        runner.add(std::move(heap));
        runner.add(std::move(tools));
        runner.add(std::move(process));
        runner.add(std::move(subprocess_v2));
        runner.add(std::move(threads));

        if (alloc_tracking_available())
                std::cout << YELLOW << "(note: glibc mallinfo2 allocation "
                          << "tracking active)" << RESET << "\n";
        if (running_under_asan())
                std::cout << YELLOW << "(note: running under AddressSanitizer)"
                          << RESET << "\n";

        return runner.run_all();
}

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>

#include "ftest.hpp"

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

        Runner runner;
        runner.add(std::move(assertions));
        runner.add(std::move(fds));
        runner.add(std::move(heap));
        runner.add(std::move(tools));
        runner.add(std::move(process));

        if (alloc_tracking_available())
                std::cout << YELLOW << "(note: glibc mallinfo2 allocation "
                          << "tracking active)" << RESET << "\n";
        if (running_under_asan())
                std::cout << YELLOW << "(note: running under AddressSanitizer)"
                          << RESET << "\n";

        return runner.run_all();
}

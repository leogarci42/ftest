#pragma once

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "framework.hpp"

namespace ftest {

struct RunResult
{
        int         status = -1;
        bool        timed_out = false;
        std::string output;
};

inline std::string describe_exit(const RunResult& result)
{
        if (result.timed_out)
                return "timed out (possible hang)";
        if (WIFSIGNALED(result.status))
                return std::string("crashed: killed by signal ")
                        + strsignal(WTERMSIG(result.status));
        if (WIFEXITED(result.status))
                return "exit code " + std::to_string(WEXITSTATUS(result.status));
        return "unknown termination";
}

class CommandRunner
{
public:
        explicit CommandRunner(std::string program) : program_(std::move(program))
        {
                static const bool sigpipe_ignored = [] {
                        struct sigaction action;
                        memset(&action, 0, sizeof(action));
                        action.sa_handler = SIG_IGN;
                        sigaction(SIGPIPE, &action, nullptr);
                        return true;
                }();
                (void)sigpipe_ignored;
        }

        RunResult run(const std::vector<std::string>& args,
                      const std::string& stdin_data = std::string(),
                      int timeout_ms = 5000,
                      int rlimit_nofile = 0) const
        {
                int in_pipe[2];
                int out_pipe[2];

                if (pipe(in_pipe) != 0)
                        throw std::runtime_error(std::string("pipe: ")
                                                 + strerror(errno));
                if (pipe(out_pipe) != 0)
                        throw std::runtime_error(std::string("pipe: ")
                                                 + strerror(errno));

                pid_t pid = fork();
                if (pid < 0)
                        throw std::runtime_error(std::string("fork: ")
                                                 + strerror(errno));

                if (pid == 0)
                {
                        if (rlimit_nofile > 0)
                        {
                                rlimit lim = {
                                        static_cast<rlim_t>(rlimit_nofile),
                                        static_cast<rlim_t>(rlimit_nofile)};
                                setrlimit(RLIMIT_NOFILE, &lim);
                        }
                        dup2(in_pipe[0], STDIN_FILENO);
                        dup2(out_pipe[1], STDOUT_FILENO);
                        dup2(out_pipe[1], STDERR_FILENO);
                        close(in_pipe[0]);
                        close(in_pipe[1]);
                        close(out_pipe[0]);
                        close(out_pipe[1]);

                        std::vector<char*> argv;
                        argv.push_back(const_cast<char*>(program_.c_str()));
                        for (const std::string& arg : args)
                                argv.push_back(const_cast<char*>(arg.c_str()));
                        argv.push_back(nullptr);

                        execvp(program_.c_str(), argv.data());
                        _exit(127);
                }

                close(in_pipe[0]);
                close(out_pipe[1]);
                write_all(in_pipe[1], stdin_data);
                close(in_pipe[1]);

                return collect(pid, out_pipe[0], timeout_ms);
        }

private:
        static void write_all(int fd, const std::string& data)
        {
                size_t written = 0;
                while (written < data.size())
                {
                        ssize_t n = write(fd, data.data() + written,
                                          data.size() - written);
                        if (n < 0)
                        {
                                if (errno == EINTR)
                                        continue;
                                break;
                        }
                        written += static_cast<size_t>(n);
                }
        }

        static RunResult collect(pid_t pid, int out_fd, int timeout_ms)
        {
                RunResult result;
                const auto deadline =
                        std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeout_ms);
                std::vector<char> buffer(4096);

                for (;;)
                {
                        auto remaining = std::chrono::duration_cast<
                                std::chrono::milliseconds>(
                                deadline - std::chrono::steady_clock::now())
                                                 .count();
                        if (remaining <= 0)
                        {
                                result.timed_out = true;
                                kill(pid, SIGKILL);
                                break;
                        }

                        pollfd fds = {out_fd, POLLIN, 0};
                        int ready = poll(&fds, 1, static_cast<int>(remaining));
                        if (ready < 0)
                        {
                                if (errno == EINTR)
                                        continue;
                                throw std::runtime_error(
                                        std::string("poll: ") + strerror(errno));
                        }
                        if (ready == 0)
                        {
                                result.timed_out = true;
                                kill(pid, SIGKILL);
                                break;
                        }

                        ssize_t n = read(out_fd, buffer.data(), buffer.size());
                        if (n == 0)
                                break;
                        if (n < 0)
                        {
                                if (errno == EINTR)
                                        continue;
                                throw std::runtime_error(
                                        std::string("read: ") + strerror(errno));
                        }
                        result.output.append(buffer.data(),
                                             static_cast<size_t>(n));
                }

                close(out_fd);
                waitpid(pid, &result.status, 0);
                return result;
        }

        std::string program_;
};

} // namespace ftest

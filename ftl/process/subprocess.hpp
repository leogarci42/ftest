#pragma once

#if defined(__GLIBC__) || defined(__MUSL__)
#define FTL_GNU_SOURCE_WAS_NOT_DEFINED 1
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "../core/error.hpp"
#include "../core/features.hpp"
#include "../io/pipe.hpp"

namespace ftl {

class RunResult
{
public:
        int         status = -1;
        bool        timed_out = false;
        std::string output;       // child stdout (merged with stderr unless
                                  // split_output() was requested)
        std::string error_output; // child stderr, only with split_output()

        bool exited() const { return WIFEXITED(status); }
        bool signaled() const { return WIFSIGNALED(status); }

        int exit_code() const
        {
                return exited() ? WEXITSTATUS(status) : -1;
        }

        int signal_number() const
        {
                return signaled() ? WTERMSIG(status) : 0;
        }
};

inline std::string describe_exit(const RunResult& result)
{
        if (result.timed_out)
                return "timed out (possible hang)";
        if (result.signaled())
                return std::string("crashed: killed by signal ")
                        + strsignal(result.signal_number());
        if (result.exited())
                return "exit code " + std::to_string(result.exit_code());
        return "unknown termination";
}

// Builder-style subprocess runner.
//
//     ftl::RunResult r = ftl::Subprocess("./app")
//             .args({"md5", "-s", "abc"})
//             .stdin_data("payload")
//             .timeout(std::chrono::milliseconds{5000})
//             .run();
//
// A hanging child is SIGKILLed at the deadline and reaped, so run() always
// returns. Output collection is driven by poll() against the deadline.
class Subprocess
{
public:
        explicit Subprocess(std::string program) : program_(std::move(program))
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

        Subprocess& args(std::vector<std::string> args)
        {
                args_ = std::move(args);
                return *this;
        }

        Subprocess& args(std::initializer_list<const char*> args)
        {
                args_.assign(args.begin(), args.end());
                return *this;
        }

        Subprocess& arg(std::string value)
        {
                args_.push_back(std::move(value));
                return *this;
        }

        Subprocess& stdin_data(std::string data)
        {
                stdin_data_ = std::move(data);
                return *this;
        }

        // Environment entry applied on top of (replace=false, default) or
        // instead of (replace=true) the parent environment.
        Subprocess& env(std::string key, std::string value,
                        bool replace = false)
        {
                if (replace)
                {
                        env_overrides_.clear();
                        env_replace_ = true;
                }
                for (auto& entry : env_overrides_)
                        if (entry.first == key)
                        {
                                entry.second = std::move(value);
                                return *this;
                        }
                env_overrides_.push_back({std::move(key), std::move(value)});
                return *this;
        }

        template <typename Rep, typename Period>
        Subprocess& timeout(std::chrono::duration<Rep, Period> duration)
        {
                timeout_ms_ = std::max(
                        1ll,
                        static_cast<long long>(
                                std::chrono::duration_cast<
                                        std::chrono::milliseconds>(duration)
                                        .count()));
                return *this;
        }

        Subprocess& timeout_ms(long long milliseconds)
        {
                timeout_ms_ = std::max(1ll, milliseconds);
                return *this;
        }

        Subprocess& rlimit_nofile(int limit)
        {
                rlimit_nofile_ = limit;
                return *this;
        }

        // Collect stdout and stderr into separate buffers instead of merging
        // both into output.
        Subprocess& split_output(bool split = true)
        {
                split_output_ = split;
                return *this;
        }

        RunResult run()
        {
                Pipe     in_pipe = Pipe::create();
                Pipe     out_pipe = Pipe::create();
                UniqueFd err_read;
                UniqueFd err_write;
                if (split_output_)
                {
                        Pipe err_pipe = Pipe::create();
                        err_read = std::move(err_pipe.read_fd);
                        err_write = std::move(err_pipe.write_fd);
                }

                pid_t pid = fork();
                if (pid < 0)
                        throw_errno("fork");

                if (pid == 0)
                        child_exec(in_pipe.read_fd.get(), in_pipe.write_fd.get(),
                                   out_pipe.read_fd.get(),
                                   out_pipe.write_fd.get(), err_read.get(),
                                   err_write.get());

                in_pipe.read_fd.reset();
                out_pipe.write_fd.reset();
                err_write.reset();

                write_all(in_pipe.write_fd.get(), stdin_data_);
                in_pipe.write_fd.reset();

                RunResult result = collect(
                        pid, out_pipe.read_fd.release(), err_read.release(),
                        timeout_ms_);
                return result;
        }

private:
        [[noreturn]] void child_exec(int in_fd, int in_write_fd, int out_read_fd,
                                     int out_fd, int err_read_fd,
                                     int err_fd) const
        {
                if (rlimit_nofile_ > 0)
                {
                        rlimit lim = {static_cast<rlim_t>(rlimit_nofile_),
                                      static_cast<rlim_t>(rlimit_nofile_)};
                        setrlimit(RLIMIT_NOFILE, &lim);
                }
                dup2(in_fd, STDIN_FILENO);
                dup2(out_fd, STDOUT_FILENO);
                if (err_fd >= 0)
                        dup2(err_fd, STDERR_FILENO);
                else
                        dup2(out_fd, STDERR_FILENO);

                // Drop the pipe ends we inherited through fork but never
                // dup2'd. In particular the stdin write end must not survive
                // here: while it stays open the child never sees EOF on
                // stdin and hangs waiting for input that will not come.
                auto close_unused = [](int fd) {
                        if (fd > STDERR_FILENO)
                                close(fd);
                };
                close_unused(in_fd);
                close_unused(in_write_fd);
                close_unused(out_read_fd);
                close_unused(out_fd);
                close_unused(err_read_fd);
                close_unused(err_fd);

                std::vector<char*> argv;
                argv.push_back(const_cast<char*>(program_.c_str()));
                for (const std::string& arg : args_)
                        argv.push_back(const_cast<char*>(arg.c_str()));
                argv.push_back(nullptr);

                if (!env_overrides_.empty())
                {
                        std::vector<std::string> storage = build_child_env();
                        std::vector<char*> envp;
                        for (const std::string& entry : storage)
                                envp.push_back(
                                        const_cast<char*>(entry.c_str()));
                        envp.push_back(nullptr);
#if defined(FTL_HAVE_EXECVPE)
                        execvpe(program_.c_str(), argv.data(), envp.data());
#else
                        execve_portable(program_.c_str(), argv.data(),
                                        envp.data());
#endif
                }
                else
                {
                        execvp(program_.c_str(), argv.data());
                }
                _exit(127);
        }

        // Strict-POSIX execvpe replacement: resolves the program against the
        // PATH of the CHILD environment (envp), then calls execve.
        static void execve_portable(const char* program, char* const* argv,
                                    char* const* envp)
        {
                if (strchr(program, '/'))
                {
                        execve(program, argv, envp);
                        return;
                }
                std::string path = "/usr/bin:/bin";
                for (char* const* e = envp; *e; ++e)
                        if (strncmp(*e, "PATH=", 5) == 0)
                        {
                                path = *e + 5;
                                break;
                        }
                size_t start = 0;
                while (start <= path.size())
                {
                        size_t end = path.find(':', start);
                        std::string dir =
                                path.substr(start, end == std::string::npos
                                                    ? std::string::npos
                                                    : end - start);
                        if (dir.empty())
                                dir = ".";
                        if (dir.back() != '/')
                                dir += '/';
                        std::string candidate = dir + program;
                        execve(candidate.c_str(), argv, envp);
                        if (end == std::string::npos)
                                break;
                        start = end + 1;
                }
        }

        std::vector<std::string> build_child_env() const
        {
                std::vector<std::string> entries;
                if (!env_replace_)
                        for (char** e = environ; e && *e; ++e)
                                entries.push_back(*e);

                for (const auto& override_pair : env_overrides_)
                {
                        std::string prefix = override_pair.first + "=";
                        bool replaced = false;
                        for (std::string& existing : entries)
                                if (existing.rfind(prefix, 0) == 0)
                                {
                                        existing = prefix + override_pair.second;
                                        replaced = true;
                                        break;
                                }
                        if (!replaced)
                                entries.push_back(prefix + override_pair.second);
                }
                return entries;
        }

        static void drain_once(int fd, std::string* sink, bool* still_open)
        {
                char buffer[4096];
                ssize_t got;
                do
                {
                        got = read(fd, buffer, sizeof(buffer));
                } while (got < 0 && errno == EINTR);

                if (got > 0)
                        sink->append(buffer, static_cast<size_t>(got));
                else
                        *still_open = false;
        }

        static RunResult collect(pid_t pid, int out_fd, int err_fd,
                                 long long timeout_ms)
        {
                RunResult result;
                auto deadline = std::chrono::steady_clock::now()
                                + std::chrono::milliseconds(timeout_ms);

                UniqueFd owned_out(out_fd);
                UniqueFd owned_err(err_fd >= 0 ? err_fd : -1);

                struct Source
                {
                        int         fd;
                        std::string* sink;
                        bool        open;
                };
                Source sources[2] = {{out_fd, &result.output, true},
                                     {err_fd, &result.error_output, false}};
                if (err_fd >= 0)
                        sources[1].open = true;

                while (sources[0].open || sources[1].open)
                {
                        long long remaining_ms =
                                std::chrono::duration_cast<
                                        std::chrono::milliseconds>(
                                        deadline
                                        - std::chrono::steady_clock::now())
                                        .count();
                        if (remaining_ms <= 0)
                        {
                                result.timed_out = true;
                                kill(pid, SIGKILL);
                                break;
                        }

                        struct pollfd watched[2];
                        int mapping[2];
                        nfds_t count = 0;
                        for (int i = 0; i < 2; ++i)
                                if (sources[i].open)
                                {
                                        watched[count] = {sources[i].fd, POLLIN,
                                                          0};
                                        mapping[count] = i;
                                        ++count;
                                }

                        int ready = poll(watched, count,
                                         static_cast<int>(remaining_ms));
                        if (ready < 0)
                        {
                                if (errno == EINTR)
                                        continue;
                                throw_errno("poll");
                        }
                        if (ready == 0)
                        {
                                result.timed_out = true;
                                kill(pid, SIGKILL);
                                break;
                        }

                        for (nfds_t k = 0; k < count; ++k)
                                if (watched[k].revents
                                    & (POLLIN | POLLHUP | POLLERR))
                                {
                                        Source& src =
                                                sources[mapping[k]];
                                        drain_once(src.fd, src.sink,
                                                   &src.open);
                                }
                }

                waitpid(pid, &result.status, 0);
                return result;
        }

        std::string              program_;
        std::vector<std::string> args_;
        std::string              stdin_data_;
        std::vector<std::pair<std::string, std::string>> env_overrides_;
        bool                     env_replace_ = false;
        long long                timeout_ms_ = 5000;
        int                      rlimit_nofile_ = 0;
        bool                     split_output_ = false;
};

} // namespace ftl

#ifdef FTL_GNU_SOURCE_WAS_NOT_DEFINED
#undef _GNU_SOURCE
#undef FTL_GNU_SOURCE_WAS_NOT_DEFINED
#endif

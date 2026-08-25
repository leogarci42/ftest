#pragma once

#include <dirent.h>
#include <pthread.h>
#if defined(__GLIBC__) || defined(__MUSL__)
#define FTL_GNU_SOURCE_WAS_NOT_DEFINED 1
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../core/features.hpp"

namespace ftl {

struct ThreadSnapshot
{
        size_t count = 0;
};

// Counts threads of the current process via /proc/self/task (Linux).
inline ThreadSnapshot thread_snapshot()
{
        ThreadSnapshot snapshot;
        DIR* dir = opendir("/proc/self/task");
        if (!dir)
                return snapshot;

        errno = 0;
        for (dirent* entry = readdir(dir); entry; entry = readdir(dir))
        {
                char* end = nullptr;
                long value = strtol(entry->d_name, &end, 10);
                if (end && *end == '\0' && value > 0)
                        ++snapshot.count;
                errno = 0;
        }
        closedir(dir);
        return snapshot;
}

// Watches raw pthread workers and verifies each finishes within a deadline.
//
//     pthread_t worker;
//     pthread_create(&worker, nullptr, run_job, &job);
//     ftl::StuckThreadGuard guard("worker pool");
//     guard.watch(worker, "worker");
//     ... work ...
//     guard.require_all_finished(std::chrono::milliseconds{2000});
//
// Ownership rules:
// * when require_all_finished() returns normally, every watched thread was
//   joined by the guard — do not join or detach those handles again;
// * when it throws, the named threads are untouched: join them yourself or
//   pthread_detach() them to clean up.
//
// Use raw pthread_create for watched workers; pairing this with std::thread
// is unsafe because the guard reaps through the native handle.
class StuckThreadGuard
{
public:
        explicit StuckThreadGuard(std::string label) : label_(std::move(label))
        {
        }

        StuckThreadGuard(const StuckThreadGuard&) = delete;
        StuckThreadGuard& operator=(const StuckThreadGuard&) = delete;

        void watch(pthread_t handle, std::string label)
        {
                watched_.push_back({handle, std::move(label)});
        }

        void require_all_finished(std::chrono::milliseconds timeout) const
        {
                auto deadline = std::chrono::steady_clock::now() + timeout;
                std::vector<std::string> stuck;

                for (const Watched& thread : watched_)
                        if (!thread_finished(thread.handle, deadline))
                                stuck.push_back(thread.label);

#if !defined(FTL_HAVE_TIMEDJOIN_NP)
                // Fallback mode never reaps threads: the caller owns
                // join/detach for every watched handle.
#endif
                if (!stuck.empty())
                {
                        std::string list;
                        for (size_t i = 0; i < stuck.size(); ++i)
                                list += (i ? ", " : "") + stuck[i];
                        throw std::runtime_error(
                                label_ + ": " + std::to_string(stuck.size())
                                + " stuck thread(s) after "
                                + std::to_string(
                                        std::chrono::duration_cast<
                                                std::chrono::milliseconds>(
                                                timeout)
                                                .count())
                                + " ms: " + list);
                }
        }

private:
        // Waits until the thread terminates or the deadline passes.
        //
        // With pthread_timedjoin_np (glibc/musl) a finished thread is REAPED
        // by the guard — do not join or detach that handle afterwards.
        //
        // Fallback (strict POSIX): probes liveness with pthread_kill(handle,
        // 0) in slices; nothing is ever reaped, so the caller owns
        // join/detach for every watched handle regardless of the outcome.
        static bool thread_finished(pthread_t handle,
                                    std::chrono::steady_clock::time_point
                                            deadline)
        {
#if defined(FTL_HAVE_TIMEDJOIN_NP)
                for (;;)
                {
                        timespec spec = timespec_from(deadline);
                        int rc = pthread_timedjoin_np(handle, nullptr, &spec);
                        if (rc == 0)
                                return true;
                        if (rc == ESRCH || rc == EINVAL)
                                return true; // already gone
                        if (std::chrono::steady_clock::now() >= deadline)
                                return false;
                }
#else
                for (;;)
                {
                        if (pthread_kill(handle, 0) == ESRCH)
                                return true;
                        if (std::chrono::steady_clock::now() >= deadline)
                                return false;
                        std::this_thread::sleep_for(
                                std::chrono::milliseconds{10});
                }
#endif
        }

        static struct timespec timespec_from(
                std::chrono::steady_clock::time_point point)
        {
                using namespace std::chrono;
                nanoseconds remaining = point - steady_clock::now();
                if (remaining < remaining.zero())
                        remaining = remaining.zero();
                timespec spec;
                spec.tv_sec =
                        duration_cast<seconds>(remaining).count();
                spec.tv_nsec =
                        (remaining
                         - duration_cast<seconds>(remaining))
                                .count();
                return spec;
        }

        struct Watched
        {
                pthread_t   handle;
                std::string label;
        };

        std::string          label_;
        std::vector<Watched> watched_;
};

} // namespace ftl

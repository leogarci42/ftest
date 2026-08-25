#pragma once

#include <pthread.h>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../ftl/ftl.hpp"
#include "framework.hpp"

namespace ftest {

// Fails the test when threads created inside the guarded scope were not
// joined before require_clean() runs.
//
// Note: a thread that exited but was never joined still shows up in
// /proc/self/task — joining is part of clean teardown.
class ThreadLeakGuard
{
public:
        explicit ThreadLeakGuard(std::string label) : label_(std::move(label))
        {
                baseline_ = ftl::thread_snapshot().count;
        }

        ThreadLeakGuard(const ThreadLeakGuard&) = delete;
        ThreadLeakGuard& operator=(const ThreadLeakGuard&) = delete;

        void require_clean(size_t expected_new_threads = 0) const
        {
                // A just-joined pthread can linger in /proc/self/task for a
                // few moments after pthread_join() returns: the kernel wakes
                // the joiner (CLONE_CHILD_CLEARTID) early in do_exit(), but
                // the task entry is unhashed from /proc only later. Poll
                // briefly so a fully-reaped thread is not misreported; a
                // genuinely unjoined thread stays visible and still fails.
                const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds{500};
                for (;;)
                {
                        size_t now = ftl::thread_snapshot().count;
                        if (now <= baseline_ + expected_new_threads)
                                return;
                        if (std::chrono::steady_clock::now() >= deadline)
                                throw TestFailure(
                                        label_ + ": "
                                        + std::to_string(now - baseline_)
                                        + " thread(s) not joined (baseline "
                                        + std::to_string(baseline_) + ", now "
                                        + std::to_string(now) + ")");
                        std::this_thread::sleep_for(
                                std::chrono::milliseconds{10});
                }
        }

private:
        std::string label_;
        size_t      baseline_ = 0;
};

// Verifies watched raw pthread workers finish within a deadline; names the
// stuck ones otherwise. See ftl::StuckThreadGuard for the ownership rules.
class StuckThreadGuard
{
public:
        explicit StuckThreadGuard(std::string label)
                : guard_(std::move(label))
        {
        }

        StuckThreadGuard(const StuckThreadGuard&) = delete;
        StuckThreadGuard& operator=(const StuckThreadGuard&) = delete;

        void watch(pthread_t handle, std::string label)
        {
                guard_.watch(handle, std::move(label));
        }

        template <typename Rep, typename Period>
        void require_all_finished(
                std::chrono::duration<Rep, Period> timeout) const
        {
                guard_.require_all_finished(timeout);
        }

private:
        ftl::StuckThreadGuard guard_;
};

} // namespace ftest

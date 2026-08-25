#pragma once

#include <pthread.h>
#include <string>
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
                size_t now = ftl::thread_snapshot().count;
                if (now > baseline_ + expected_new_threads)
                        throw TestFailure(
                                label_ + ": " + std::to_string(now - baseline_)
                                + " thread(s) not joined (baseline "
                                + std::to_string(baseline_) + ", now "
                                + std::to_string(now) + ")");
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

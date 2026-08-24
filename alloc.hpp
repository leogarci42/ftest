#pragma once

#if defined(__GLIBC__)
#define FTEST_GNU_SOURCE_WAS_NOT_DEFINED 1
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <malloc.h>
#endif

#include <string>

#include "framework.hpp"

namespace ftest {

inline bool running_under_asan()
{
#if defined(__SANITIZE_ADDRESS__)
        return true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
        return true;
#endif
#endif
        return false;
}

struct AllocSnapshot
{
        long allocated_bytes = -1;
        long mmapped_bytes = -1;
};

inline bool alloc_tracking_available()
{
#if defined(__GLIBC__)
        return !running_under_asan();
#else
        return false;
#endif
}

inline AllocSnapshot alloc_snapshot()
{
#if defined(__GLIBC__)
        struct mallinfo2 info = mallinfo2();
        return {static_cast<long>(info.uordblks),
                static_cast<long>(info.hblkhd)};
#else
        return {-1, -1};
#endif
}

class AllocGuard
{
public:
        explicit AllocGuard(std::string label) : label_(std::move(label))
        {
                baseline_ = alloc_snapshot();
        }

        AllocGuard(const AllocGuard&) = delete;
        AllocGuard& operator=(const AllocGuard&) = delete;

        void require_no_heap_growth(long tolerance_bytes = 0) const
        {
                if (!alloc_tracking_available())
                        return;

                AllocSnapshot now = alloc_snapshot();
                long delta = now.allocated_bytes - baseline_.allocated_bytes;
                long mmap_delta = now.mmapped_bytes - baseline_.mmapped_bytes;

                if (delta > tolerance_bytes || mmap_delta > tolerance_bytes)
                        throw TestFailure(
                                label_ + ": heap grew by " + std::to_string(delta)
                                + " bytes in use (" + std::to_string(mmap_delta)
                                + " bytes mmapped), tolerance is "
                                + std::to_string(tolerance_bytes));
        }

private:
        std::string   label_;
        AllocSnapshot baseline_;
};

} // namespace ftest

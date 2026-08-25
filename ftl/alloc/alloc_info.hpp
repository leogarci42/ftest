#pragma once

#if defined(__GLIBC__)
#define FTL_GNU_SOURCE_WAS_NOT_DEFINED 1
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <malloc.h>
#endif

namespace ftl {

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
        return {};
#endif
}

} // namespace ftl

#ifdef FTL_GNU_SOURCE_WAS_NOT_DEFINED
#undef _GNU_SOURCE
#undef FTL_GNU_SOURCE_WAS_NOT_DEFINED
#endif

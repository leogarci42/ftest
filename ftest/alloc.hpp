#pragma once

#include <string>
#include <utility>

#include "../ftl/ftl.hpp"
#include "framework.hpp"

namespace ftest {

using ftl::AllocSnapshot;
using ftl::alloc_snapshot;
using ftl::alloc_tracking_available;
using ftl::running_under_asan;

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

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "framework.hpp"

namespace ftest {

using ftl::fd_snapshot;

class FdLeakGuard
{
public:
        explicit FdLeakGuard(std::string label) : label_(std::move(label))
        {
                baseline_ = fd_snapshot();
        }

        FdLeakGuard(const FdLeakGuard&) = delete;
        FdLeakGuard& operator=(const FdLeakGuard&) = delete;

        void require_clean() const
        {
                std::vector<int> now = fd_snapshot();
                std::vector<int> leaked;
                for (int fd : now)
                        if (!std::binary_search(baseline_.begin(),
                                                baseline_.end(), fd))
                                leaked.push_back(fd);

                if (!leaked.empty())
                {
                        std::string list;
                        for (size_t i = 0; i < leaked.size(); ++i)
                                list += (i ? ", " : "")
                                        + std::to_string(leaked[i]);
                        throw TestFailure(label_ + ": "
                                          + std::to_string(leaked.size())
                                          + " file descriptor(s) left open: "
                                          + list);
                }
        }

private:
        std::string      label_;
        std::vector<int> baseline_;
};

} // namespace ftest

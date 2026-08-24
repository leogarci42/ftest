#pragma once

#include <dirent.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include "framework.hpp"

namespace ftest {

inline std::vector<int> fd_snapshot()
{
        int dir_fd = open("/proc/self/fd", O_RDONLY | O_DIRECTORY);
        if (dir_fd < 0)
                return {};

        DIR* dir = fdopendir(dir_fd);
        if (!dir)
        {
                close(dir_fd);
                return {};
        }

        std::vector<int> fds;
        errno = 0;
        for (dirent* entry = readdir(dir); entry; entry = readdir(dir))
        {
                char* end = nullptr;
                long value = strtol(entry->d_name, &end, 10);
                if (end && *end == '\0' && value >= 0)
                        fds.push_back(static_cast<int>(value));
                errno = 0;
        }
        closedir(dir);

        std::sort(fds.begin(), fds.end());
        return fds;
}

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
        std::string       label_;
        std::vector<int>  baseline_;
};

} // namespace ftest

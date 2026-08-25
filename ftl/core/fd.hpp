#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ftl {

class UniqueFd
{
public:
        UniqueFd() = default;

        explicit UniqueFd(int fd) : fd_(fd) {}

        ~UniqueFd()
        {
                if (fd_ >= 0)
                        close(fd_);
        }

        UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_)
        {
                other.fd_ = -1;
        }

        UniqueFd& operator=(UniqueFd&& other) noexcept
        {
                if (this != &other)
                {
                        if (fd_ >= 0)
                                close(fd_);
                        fd_ = other.fd_;
                        other.fd_ = -1;
                }
                return *this;
        }

        UniqueFd(const UniqueFd&) = delete;
        UniqueFd& operator=(const UniqueFd&) = delete;

        int get() const { return fd_; }

        int release() noexcept
        {
                int fd = fd_;
                fd_ = -1;
                return fd;
        }

        void reset(int fd = -1) noexcept
        {
                if (fd_ >= 0)
                        close(fd_);
                fd_ = fd;
        }

        explicit operator bool() const { return fd_ >= 0; }

private:
        int fd_ = -1;
};

inline UniqueFd open_fd(const std::string& path, int flags = O_RDONLY)
{
        int fd = open(path.c_str(), flags);
        if (fd < 0)
                throw std::runtime_error("cannot open " + path + ": "
                                         + std::string(strerror(errno)));
        return UniqueFd(fd);
}

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

} // namespace ftl

#pragma once

#include <unistd.h>
#include <cerrno>
#include <cstddef>
#include <string>

#include "../core/fd.hpp"

namespace ftl {

struct Pipe
{
        UniqueFd read_fd;
        UniqueFd write_fd;

        static Pipe create()
        {
                int fds[2];
                if (pipe(fds) != 0)
                        throw std::runtime_error(
                                std::string("pipe: ") + strerror(errno));
                return Pipe{UniqueFd(fds[0]), UniqueFd(fds[1])};
        }
};

inline void write_all(int fd, const char* data, size_t size)
{
        size_t written = 0;
        while (written < size)
        {
                ssize_t n = write(fd, data + written, size - written);
                if (n < 0)
                {
                        if (errno == EINTR)
                                continue;
                        return;
                }
                written += static_cast<size_t>(n);
        }
}

inline void write_all(int fd, const std::string& data)
{
        write_all(fd, data.data(), data.size());
}

} // namespace ftl

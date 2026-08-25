#pragma once

#include <fcntl.h>
#include <string>

#include "../ftl/ftl.hpp"

namespace ftest {

using ftl::TempDir;
using ftl::TempFile;
using ftl::UniqueFd;

// Thin wrapper keeping the original constructor style (throws on failure).
class FileDescriptor
{
public:
        explicit FileDescriptor(const std::string& path, int flags = O_RDONLY)
                : fd_(ftl::open_fd(path, flags))
        {
        }

        int get() const { return fd_.get(); }

private:
        ftl::UniqueFd fd_;
};

} // namespace ftest

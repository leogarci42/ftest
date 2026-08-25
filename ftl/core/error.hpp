#pragma once

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace ftl {

[[noreturn]] inline void throw_errno(const std::string& what)
{
        throw std::runtime_error(what + ": " + std::string(strerror(errno)));
}

} // namespace ftl

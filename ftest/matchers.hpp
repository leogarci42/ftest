#pragma once

#include <sys/wait.h>
#include <string>

#include "../ftl/ftl.hpp"
#include "framework.hpp"

namespace ftest {

inline void require_exit(const RunResult& result, int expected_code,
                         const std::string& what)
{
        require(!result.timed_out, what + ": process timed out ("
                                           + describe_exit(result) + ")");
        require(result.exited(), what + ": process did not exit normally ("
                                         + describe_exit(result) + ")");
        require_equal(static_cast<long long>(result.exit_code()),
                      static_cast<long long>(expected_code), what);
}

inline void require_signaled(const RunResult& result, int expected_signal,
                             const std::string& what)
{
        require(result.signaled(),
                what + ": expected a crash but " + describe_exit(result));
        if (expected_signal > 0)
                require_equal(static_cast<long long>(result.signal_number()),
                              static_cast<long long>(expected_signal),
                              what + ": wrong terminating signal");
}

inline void require_timed_out(const RunResult& result,
                              const std::string& what)
{
        require(result.timed_out, what + ": expected a timeout but "
                                          + describe_exit(result));
}

inline void require_output_contains(const RunResult& result,
                                    const std::string& needle,
                                    const std::string& what)
{
        require_contains(result.output, needle, what);
}

} // namespace ftest

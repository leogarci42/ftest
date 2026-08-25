#pragma once

#include <string>
#include <utility>
#include <vector>

#include "../ftl/ftl.hpp"

namespace ftest {

using ftl::RunResult;
using ftl::Subprocess;
using ftl::describe_exit;

// Backward-compatible wrapper around ftl::Subprocess.
class CommandRunner
{
public:
        explicit CommandRunner(std::string program)
                : subprocess_(std::move(program))
        {
        }

        RunResult run(const std::vector<std::string>& args,
                      const std::string& stdin_data = std::string(),
                      int timeout_ms = 5000,
                      int rlimit_nofile = 0) const
        {
                Subprocess proc = subprocess_;
                return proc.args(args)
                        .stdin_data(stdin_data)
                        .timeout_ms(timeout_ms)
                        .rlimit_nofile(rlimit_nofile)
                        .run();
        }

private:
        // Copied per run() so the same runner can be reused with different
        // options without state leaking between invocations.
        Subprocess subprocess_;
};

} // namespace ftest

#pragma once

#include <unistd.h>
#include <cstdio>
#include <functional>
#include <string>
#include <stdexcept>

#include "framework.hpp"

namespace ftest {

class OutputCapture
{
public:
        OutputCapture()
        {
                fflush(stdout);
                file_ = tmpfile();
                saved_ = dup(STDOUT_FILENO);
                if (!file_ || saved_ < 0)
                        throw std::runtime_error("cannot redirect stdout");
                dup2(fileno(file_), STDOUT_FILENO);
        }

        ~OutputCapture()
        {
                fflush(stdout);
                if (saved_ >= 0)
                {
                        dup2(saved_, STDOUT_FILENO);
                        close(saved_);
                }
                if (file_)
                        fclose(file_);
        }

        OutputCapture(const OutputCapture&) = delete;
        OutputCapture& operator=(const OutputCapture&) = delete;

        std::string content()
        {
                fflush(stdout);
                long size = ftell(file_);
                require(size >= 0, "ftell failed on capture buffer");
                rewind(file_);
                std::string out(static_cast<size_t>(size), '\0');
                if (size > 0 && fread(&out[0], 1, static_cast<size_t>(size), file_)
                                != static_cast<size_t>(size))
                        throw std::runtime_error("fread failed on capture buffer");
                return out;
        }

private:
        FILE* file_ = nullptr;
        int   saved_ = -1;
};

inline std::string capture_stdout(const std::function<void()>& fn)
{
        OutputCapture capture;
        fn();
        return capture.content();
}

} // namespace ftest

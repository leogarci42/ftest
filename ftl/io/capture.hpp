#pragma once

#include <unistd.h>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>

namespace ftl {

// Redirects an existing file descriptor to a temporary in-memory-backed file
// for the lifetime of this object. Restores the original descriptor on
// destruction, even when an exception unwinds through the scope.
//
// For descriptors backed by stdio streams (e.g. STDOUT_FILENO) call flush()
// or rely on content(), which flushes the stream for you.
class FdCapture
{
public:
        explicit FdCapture(int fd, FILE* stream_to_flush = nullptr)
                : target_(fd), stream_(stream_to_flush)
        {
                if (stream_)
                        fflush(stream_);
                file_ = tmpfile();
                saved_ = dup(target_);
                if (!file_ || saved_ < 0)
                {
                        if (file_)
                                fclose(file_);
                        if (saved_ >= 0)
                        {
                                close(saved_);
                                saved_ = -1;
                        }
                        throw std::runtime_error("cannot redirect file "
                                                 "descriptor");
                }
                dup2(fileno(file_), target_);
        }

        ~FdCapture()
        {
                if (stream_)
                        fflush(stream_);
                if (saved_ >= 0)
                {
                        dup2(saved_, target_);
                        close(saved_);
                }
                if (file_)
                        fclose(file_);
        }

        FdCapture(const FdCapture&) = delete;
        FdCapture& operator=(const FdCapture&) = delete;

        std::string content()
        {
                if (stream_)
                        fflush(stream_);
                long size = ftell(file_);
                if (size < 0)
                        throw std::runtime_error(
                                "ftell failed on capture buffer");
                rewind(file_);
                std::string out(static_cast<size_t>(size), '\0');
                if (size > 0 && fread(&out[0], 1, static_cast<size_t>(size),
                                      file_)
                                != static_cast<size_t>(size))
                        throw std::runtime_error(
                                "fread failed on capture buffer");
                return out;
        }

private:
        int   target_;
        FILE* stream_;
        FILE* file_ = nullptr;
        int   saved_ = -1;
};

using OutputCapture = FdCapture;

inline std::string capture_stdout(const std::function<void()>& fn)
{
        OutputCapture capture(STDOUT_FILENO, stdout);
        fn();
        return capture.content();
}

} // namespace ftl

#pragma once

#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <cstdlib>
#include <fstream>
#include <string>
#include <stdexcept>

#include "framework.hpp"

namespace ftest {

class FileDescriptor
{
public:
        explicit FileDescriptor(const std::string& path, int flags = O_RDONLY)
                : fd_(open(path.c_str(), flags))
        {
                if (fd_ < 0)
                        throw std::runtime_error("cannot open " + path + ": "
                                                 + std::string(strerror(errno)));
        }

        ~FileDescriptor()
        {
                if (fd_ >= 0)
                        close(fd_);
        }

        FileDescriptor(const FileDescriptor&) = delete;
        FileDescriptor& operator=(const FileDescriptor&) = delete;

        int get() const { return fd_; }

private:
        int fd_;
};

class TempFile
{
public:
        TempFile(const std::string& path, const std::string& content)
                : path_(path)
        {
                std::ofstream out(path_, std::ios::binary | std::ios::trunc);
                require(static_cast<bool>(out),
                        "cannot create temp file " + path_);
                out << content;
        }

        ~TempFile()
        {
                chmod(path_.c_str(), 0644);
                std::remove(path_.c_str());
        }

        TempFile(const TempFile&) = delete;
        TempFile& operator=(const TempFile&) = delete;

        const std::string& path() const { return path_; }

private:
        std::string path_;
};

class TempDir
{
public:
        TempDir(const std::string& prefix)
        {
                std::string tmpl = "/tmp/" + prefix + "_XXXXXX";
                std::vector<char> buf(tmpl.begin(), tmpl.end());
                buf.push_back('\0');
                char* result = mkdtemp(buf.data());
                if (!result)
                        throw std::runtime_error("mkdtemp failed for " + prefix);
                path_ = result;
        }

        ~TempDir() { std::remove(path_.c_str()); }

        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;

        const std::string& path() const { return path_; }

private:
        std::string path_;
};

} // namespace ftest

#pragma once

#include <ftw.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ftl {

class TempFile
{
public:
        TempFile(const std::string& path, const std::string& content)
                : path_(path)
        {
                std::ofstream out(path_, std::ios::binary | std::ios::trunc);
                if (!out)
                        throw std::runtime_error("cannot create temp file "
                                                 + path_);
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
        explicit TempDir(const std::string& prefix)
        {
                std::string tmpl = "/tmp/" + prefix + "_XXXXXX";
                std::vector<char> buf(tmpl.begin(), tmpl.end());
                buf.push_back('\0');
                char* result = mkdtemp(buf.data());
                if (!result)
                        throw std::runtime_error("mkdtemp failed for " + prefix);
                path_ = result;
        }

        ~TempDir() { remove_tree(path_); }

        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;

        const std::string& path() const { return path_; }

private:
        static int remove_entry(const char* path, const struct stat*, int,
                                struct FTW*)
        {
                return remove(path) == 0 ? 0 : -1;
        }

        static void remove_tree(const std::string& path)
        {
                nftw(path.c_str(), remove_entry, 16, FTW_DEPTH | FTW_PHYS);
        }

        std::string path_;
};

} // namespace ftl

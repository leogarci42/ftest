#pragma once

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ftest {

inline const char* const RESET  = "\033[0m";
inline const char* const RED    = "\033[31m";
inline const char* const GREEN  = "\033[32m";
inline const char* const YELLOW = "\033[33m";
inline const char* const BLUE   = "\033[34m";
inline const char* const BOLD   = "\033[1m";

struct TestFailure : public std::runtime_error
{
        explicit TestFailure(const std::string& message)
                : std::runtime_error(message) {}
};

inline void require(bool condition, const std::string& what)
{
        if (!condition)
                throw TestFailure(what);
}

inline void require_false(bool condition, const std::string& what)
{
        require(!condition, what);
}

template <typename T>
inline void require_throws(const std::string& what, std::function<void()> fn)
{
        bool threw = false;
        try
        {
                fn();
        }
        catch (const T&)
        {
                threw = true;
        }
        catch (...)
        {
                threw = true;
        }
        require(threw, what + ": expected an exception but none was thrown");
}

template <typename Fn>
inline void require_no_throw(const std::string& what, Fn&& fn)
{
        try
        {
                fn();
        }
        catch (const std::exception& e)
        {
                throw TestFailure(what + ": unexpected exception: " + e.what());
        }
        catch (...)
        {
                throw TestFailure(what + ": unexpected non-standard exception");
        }
}

inline void require_equal(const std::string& got, const std::string& expected,
                          const std::string& what)
{
        if (got != expected)
                throw TestFailure(
                        what +
                        "\n       " + YELLOW + "expected" + RESET + ": [" + expected
                        + "]" +
                        "\n       " + YELLOW + "got     " + RESET + ": [" + got + "]");
}

inline void require_equal(long long got, long long expected,
                          const std::string& what)
{
        if (got != expected)
                throw TestFailure(
                        what +
                        "\n       " + YELLOW + "expected" + RESET + ": "
                        + std::to_string(expected) +
                        "\n       " + YELLOW + "got     " + RESET + ": "
                        + std::to_string(got));
}

inline void require_equal(size_t got, size_t expected, const std::string& what)
{
        require_equal(static_cast<long long>(got),
                      static_cast<long long>(expected), what);
}

inline void require_contains(const std::string& haystack,
                             const std::string& needle,
                             const std::string& what)
{
        if (haystack.find(needle) == std::string::npos)
                throw TestFailure(
                        what + ": output does not contain \"" + needle + "\"" +
                        "\n       " + YELLOW + "got     " + RESET + ": ["
                        + haystack + "]");
}

class Suite
{
public:
        using Case = std::pair<std::string, std::function<void()>>;

        explicit Suite(std::string name) : name_(std::move(name)) {}

        Suite(const Suite&) = delete;
        Suite& operator=(const Suite&) = delete;
        Suite(Suite&&) = default;
        Suite& operator=(Suite&&) = default;

        Suite& add(std::string description, std::function<void()> test)
        {
                cases_.push_back({std::move(description), std::move(test)});
                return *this;
        }

        size_t size() const { return cases_.size(); }

        const std::string& name() const { return name_; }

        size_t run() const
        {
                std::cout << BLUE << "== series: " << name_ << " ("
                          << cases_.size() << " tests)" << RESET << "\n";
                size_t failed = 0;
                for (const Case& test : cases_)
                {
                        std::cout << "  - " << test.first << " ";
                        try
                        {
                                test.second();
                                std::cout << GREEN << "[PASS]" << RESET << "\n";
                        }
                        catch (const TestFailure& e)
                        {
                                ++failed;
                                std::cout << RED << "[FAIL]" << RESET << "\n"
                                          << e.what() << "\n";
                        }
                        catch (const std::exception& e)
                        {
                                ++failed;
                                std::cout << YELLOW << "[ERROR]" << RESET << "\n"
                                          << "       unexpected exception: "
                                          << e.what() << "\n";
                        }
                        catch (...)
                        {
                                ++failed;
                                std::cout << RED << "[ERROR]" << RESET << "\n"
                                          << "       unknown exception\n";
                        }
                }
                return failed;
        }

private:
        std::string       name_;
        std::vector<Case> cases_;
};

class Runner
{
public:
        Runner& add(Suite suite)
        {
                total_ += suite.size();
                suites_.push_back(std::move(suite));
                return *this;
        }

        int run_all()
        {
                size_t failed = 0;
                for (const Suite& suite : suites_)
                        failed += suite.run();

                std::cout << "\n" << BOLD;
                if (failed == 0)
                        std::cout << GREEN << "ALL " << total_
                                  << " TESTS PASSED" << RESET << "\n";
                else
                        std::cout << RED << failed << "/" << total_
                                  << " TESTS FAILED" << RESET << "\n";
                return failed == 0 ? 0 : 1;
        }

private:
        std::vector<Suite> suites_;
        size_t             total_ = 0;
};

} // namespace ftest

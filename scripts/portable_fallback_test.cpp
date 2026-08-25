// Portable-fallback probe: verifies ftl works when the GNU-extension
// feature macros are disabled (strict-POSIX build).
//
// Build with a copy of ftl/ whose features.hpp has its FTL_HAVE_* defines
// stripped (see the "portable fallback" CI step or do it by hand):
//
//   c++ -std=c++17 -pthread -I<stripped-ftl-root> portable_fallback_test.cpp
//
#include <pthread.h>
#include <unistd.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "ftl/process/subprocess.hpp"
#include "ftl/thread/thread_count.hpp"

static void* noop_worker(void*)
{
        return nullptr;
}

static void* sleeper_worker(void*)
{
        sleep(30);
        return nullptr;
}

int main()
{
        // 1) PATH resolution through the CHILD environment
        ftl::RunResult r = ftl::Subprocess("sh")
                                   .args({"-c", "echo portable-ok"})
                                   .env("PATH", "/bin:/usr/bin")
                                   .run();
        assert(r.exited() && r.exit_code() == 0);
        assert(r.output.find("portable-ok") != std::string::npos);

        // 2) absolute-path exec still works without execvpe
        r = ftl::Subprocess("/bin/sh").args({"-c", "echo abs-ok"}).run();
        assert(r.exit_code() == 0
               && r.output.find("abs-ok") != std::string::npos);

        // 3) stuck-thread fallback probe (never reaps)
        pthread_t stuck;
        pthread_t quick;
        assert(pthread_create(&stuck, nullptr, sleeper_worker, nullptr) == 0);
        assert(pthread_create(&quick, nullptr, noop_worker, nullptr) == 0);

        ftl::StuckThreadGuard guard("probe");
        guard.watch(stuck, "stuck");
        guard.watch(quick, "quick");

        bool detected = false;
        try
        {
                guard.require_all_finished(std::chrono::milliseconds{300});
        }
        catch (const std::exception& e)
        {
                detected = strstr(e.what(), "stuck") != nullptr;
        }
        assert(detected && "sleeping worker must be flagged as stuck");

        // Fallback mode never reaps: caller owns cleanup for both handles.
        pthread_detach(stuck);
        pthread_detach(quick);

        printf("ALL PORTABLE-FALLBACK TESTS PASSED\n");
        return 0;
}

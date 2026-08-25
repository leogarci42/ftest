/* ftl.h — thin pure-C API over the ftl low-level utilities.
 *
 * Self-contained C99: no C++ headers required. All functions are
 * `static inline` so the header works in any translation unit without
 * compiling a library.
 */
#ifndef FTL_H
#define FTL_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*
 * NOTE for consumers: _POSIX_C_SOURCE must be 200809L or newer BEFORE any
 * system header is included (feature macros are read by <features.h>). If
 * your translation unit includes other system headers first, define
 * _POSIX_C_SOURCE yourself at the top or via -D_POSIX_C_SOURCE=200809L.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Subprocess runner                                                   */

typedef struct
{
        int   status;     /* raw waitpid status — use WIFEXITED family  */
        int   timed_out;  /* 1 when the child had to be SIGKILLed       */
        char* output;     /* merged stdout+stderr, NUL-terminated,
                             NULL when nothing was produced             */
} ftl_run_result;

static inline void ftl_free_run_result(ftl_run_result* result)
{
        if (!result)
                return;
        free(result->output);
        result->output = NULL;
}

/*
 * Run `program` with `args` (argv[1..], NULL-terminated array, may be NULL).
 * `stdin_data` (may be NULL) is piped to the child's stdin. The child is
 * SIGKILLed after `timeout_ms` milliseconds.
 *
 * Returns 0 on success and fills *result (call ftl_free_run_result after).
 * Returns -1 on setup failure (errno set); *result is left untouched.
 * A child that fails to exec surfaces as exit code 127 inside the result.
 */
static inline int ftl_run_command(const char* program,
                                  const char* const* args,
                                  const char* stdin_data,
                                  long long timeout_ms,
                                  ftl_run_result* result)
{
        int in_pipe[2];
        int out_pipe[2];
        pid_t pid;

        if (!program || !result)
        {
                errno = EINVAL;
                return -1;
        }
        if (timeout_ms <= 0)
                timeout_ms = 5000;

        if (pipe(in_pipe) != 0)
                return -1;
        if (pipe(out_pipe) != 0)
        {
                close(in_pipe[0]);
                close(in_pipe[1]);
                return -1;
        }

        pid = fork();
        if (pid < 0)
        {
                close(in_pipe[0]);
                close(in_pipe[1]);
                close(out_pipe[0]);
                close(out_pipe[1]);
                return -1;
        }

        if (pid == 0)
        {
                dup2(in_pipe[0], STDIN_FILENO);
                dup2(out_pipe[1], STDOUT_FILENO);
                dup2(out_pipe[1], STDERR_FILENO);
                close(in_pipe[0]);
                close(in_pipe[1]);
                close(out_pipe[0]);
                close(out_pipe[1]);

                size_t argc = 1;
                if (args)
                        while (args[argc - 1])
                                ++argc;
                char** argv = (char**)calloc(argc + 1, sizeof(char*));
                if (!argv)
                        _exit(127);
                argv[0] = (char*)program;
                for (size_t i = 1; i < argc; ++i)
                        argv[i] = (char*)args[i - 1];
                argv[argc] = NULL;

                execvp(program, argv);
                _exit(127);
        }

        close(in_pipe[0]);
        close(out_pipe[1]);

        /* write stdin payload */
        if (stdin_data && *stdin_data)
        {
                size_t written = 0;
                size_t total = strlen(stdin_data);
                while (written < total)
                {
                        ssize_t n = write(in_pipe[1], stdin_data + written,
                                          total - written);
                        if (n < 0)
                        {
                                if (errno == EINTR)
                                        continue;
                                break;
                        }
                        written += (size_t)n;
                }
        }
        close(in_pipe[1]);

        /* collect output with poll against the deadline */
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        long long elapsed_ms = 0;

        size_t capacity = 4096;
        size_t length = 0;
        char* buffer = (char*)malloc(capacity);

        for (;;)
        {
                long long remaining = timeout_ms - elapsed_ms;
                if (remaining <= 0)
                {
                        kill(pid, SIGKILL);
                        break;
                }
                if ((long long)remaining > 1000000LL)
                        remaining = 1000000LL;

                struct pollfd watched = {out_pipe[0], POLLIN, 0};
                int ready = poll(&watched, 1, (int)remaining);
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                elapsed_ms = (now.tv_sec - start.tv_sec) * 1000LL
                             + (now.tv_nsec - start.tv_nsec) / 1000000LL;

                if (ready < 0)
                {
                        if (errno == EINTR)
                                continue;
                        break;
                }
                if (ready == 0)
                {
                        if (elapsed_ms >= timeout_ms)
                        {
                                kill(pid, SIGKILL);
                                break;
                        }
                        continue;
                }

                char chunk[4096];
                ssize_t got = read(out_pipe[0], chunk, sizeof(chunk));
                if (got == 0)
                        break;
                if (got < 0)
                {
                        if (errno == EINTR)
                                continue;
                        break;
                }
                if (buffer)
                {
                        if (length + (size_t)got + 1 > capacity)
                        {
                                size_t next = capacity * 2;
                                while (length + (size_t)got + 1 > next)
                                        next *= 2;
                                char* grown = (char*)realloc(buffer, next);
                                if (!grown)
                                {
                                        free(buffer);
                                        buffer = NULL;
                                }
                                else
                                {
                                        buffer = grown;
                                        capacity = next;
                                }
                        }
                        if (buffer)
                        {
                                memcpy(buffer + length, chunk, (size_t)got);
                                length += (size_t)got;
                        }
                }
        }
        close(out_pipe[0]);

        result->status = -1;
        result->timed_out = (elapsed_ms >= timeout_ms);
        waitpid(pid, &result->status, 0);

        if (buffer)
                buffer[length] = '\0';
        result->output = buffer;
        return 0;
}

/* ------------------------------------------------------------------ */
/* Temp file helpers                                                   */

/* Create a unique temp file under /tmp with prefix `prefix` and write
 * `content` into it. Returns a malloc'd absolute path, or NULL on failure. */
static inline char* ftl_temp_file_create(const char* prefix,
                                         const char* content)
{
        if (!prefix || !content)
        {
                errno = EINVAL;
                return NULL;
        }
        char pattern[256];
        snprintf(pattern, sizeof(pattern), "/tmp/%s_XXXXXX", prefix);

        int fd = mkstemp(pattern);
        if (fd < 0)
                return NULL;

        size_t total = strlen(content);
        size_t written = 0;
        while (written < total)
        {
                ssize_t n = write(fd, content + written, total - written);
                if (n < 0)
                {
                        if (errno == EINTR)
                                continue;
                        break;
                }
                written += (size_t)n;
        }
        close(fd);
        return strdup(pattern);
}

static inline void ftl_temp_file_destroy(char* path)
{
        if (!path)
                return;
        unlink(path);
        free(path);
}

#ifdef __cplusplus
}
#endif

#endif /* FTL_H */

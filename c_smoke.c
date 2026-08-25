/* Smoke test for the pure-C API in ftl/ftl.h — compiled with a C compiler. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ftl/ftl.h"

int main(void)
{
        /* subprocess: output + exit code */
        ftl_run_result result;
        memset(&result, 0, sizeof(result));
        const char* args[] = {"-c", "echo hello", NULL};
        int rc = ftl_run_command("/bin/sh", args, NULL, 5000, &result);
        assert(rc == 0);
        assert(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 0);
        assert(result.output && strcmp(result.output, "hello\n") == 0);
        ftl_free_run_result(&result);

        /* subprocess: exit code propagation */
        const char* fail_args[] = {"-c", "exit 3", NULL};
        rc = ftl_run_command("/bin/sh", fail_args, NULL, 5000, &result);
        assert(rc == 0);
        assert(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 3);
        ftl_free_run_result(&result);

        /* subprocess: stdin piping */
        const char* cat_args[] = {"-c", "cat", NULL};
        rc = ftl_run_command("/bin/sh", cat_args, "pipe me through", 5000,
                             &result);
        assert(rc == 0);
        assert(result.output && strcmp(result.output, "pipe me through") == 0);
        ftl_free_run_result(&result);

        /* subprocess: hanging child is killed at the deadline */
        const char* hang_args[] = {"-c", "sleep 30", NULL};
        rc = ftl_run_command("/bin/sh", hang_args, NULL, 300, &result);
        assert(rc == 0);
        assert(result.timed_out == 1);
        ftl_free_run_result(&result);

        /* temp file: create, write, read back, destroy */
        char* path = ftl_temp_file_create("ftl_c_smoke", "payload");
        assert(path != NULL);
        FILE* file = fopen(path, "r");
        assert(file != NULL);
        char buffer[64] = {0};
        size_t got = fread(buffer, 1, sizeof(buffer) - 1, file);
        fclose(file);
        assert(got == strlen("payload"));
        assert(strcmp(buffer, "payload") == 0);
        ftl_temp_file_destroy(path);

        printf("ALL C SMOKE TESTS PASSED\n");
        return 0;
}

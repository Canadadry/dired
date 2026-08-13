#include "minitest.h"
#include "../src/pager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int read_file_contents(const char *path, char *out, size_t out_cap)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t n = fread(out, 1, out_cap - 1, f);
    out[n] = '\0';
    fclose(f);
    return (int)n;
}

static void test_run_via_pty_forwards_producer_output_intact(void)
{
    typedef struct {
        const char *name;
        const char *producer_text;
    } Case;

    Case cases[] = {
        {"plain text", "hello from producer"},
        {"multiline text", "line one\nline two\nline three\n"},
        {"ansi escape sequence", "\x1b[31mred\x1b[0m\n"},
    };

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char tmpl[256];
        snprintf(tmpl, sizeof(tmpl), "%s/pager_test_XXXXXX", tmpdir);
        int fd = mkstemp(tmpl);
        if (fd < 0) {
            TEST_ERRORF(cases[i].name, "mkstemp failed");
            continue;
        }
        close(fd);

        char of_arg[sizeof(tmpl) + 8];
        snprintf(of_arg, sizeof(of_arg), "of=%s", tmpl);

        char *primary_argv[] = { "printf", "%s", (char *)cases[i].producer_text, NULL };
        char *pager_argv[] = { "dd", of_arg, "status=none", NULL };

        run_via_pty(primary_argv, pager_argv);

        char got[4096];
        int n = read_file_contents(tmpl, got, sizeof(got));
        unlink(tmpl);

        if (n < 0) {
            TEST_ERRORF(cases[i].name, "failed to read pager output file");
            continue;
        }
        if (strcmp(got, cases[i].producer_text) != 0) {
            TEST_ERRORF(cases[i].name, "pager received %s, want %s", got, cases[i].producer_text);
        }
    }
}

void test_pager(void)
{
    test_run_via_pty_forwards_producer_output_intact();
}

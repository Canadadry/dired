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

static int make_tmp_file(char *out_path, size_t out_path_cap)
{
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";
    snprintf(out_path, out_path_cap, "%s/pager_test_XXXXXX", tmpdir);
    int fd = mkstemp(out_path);
    if (fd < 0)
        return -1;
    close(fd);
    return 0;
}

static int failing_pty_alloc(int *master_fd, char *slave_path, size_t slave_path_len)
{
    (void)master_fd;
    (void)slave_path;
    (void)slave_path_len;
    return -1;
}

static void test_run_paged_uses_pty_pager_on_alloc_success(void)
{
    const char *producer_text = "paged via pty\n";

    char pty_out[256];
    char pipe_out[256];
    if (make_tmp_file(pty_out, sizeof(pty_out)) < 0 || make_tmp_file(pipe_out, sizeof(pipe_out)) < 0) {
        TEST_ERRORF("run_paged success", "mkstemp failed");
        return;
    }

    char pty_of_arg[sizeof(pty_out) + 8];
    char pipe_of_arg[sizeof(pipe_out) + 8];
    snprintf(pty_of_arg, sizeof(pty_of_arg), "of=%s", pty_out);
    snprintf(pipe_of_arg, sizeof(pipe_of_arg), "of=%s", pipe_out);

    char *primary_argv[] = { "printf", "%s", (char *)producer_text, NULL };
    char *pty_pager_argv[] = { "dd", pty_of_arg, "status=none", NULL };
    char *pipe_pager_argv[] = { "dd", pipe_of_arg, "status=none", NULL };

    run_paged(primary_argv, pty_pager_argv, pipe_pager_argv);

    char got_pty[4096];
    char got_pipe[4096];
    int n_pty = read_file_contents(pty_out, got_pty, sizeof(got_pty));
    int n_pipe = read_file_contents(pipe_out, got_pipe, sizeof(got_pipe));
    unlink(pty_out);
    unlink(pipe_out);

    if (n_pty < 0 || n_pipe < 0) {
        TEST_ERRORF("run_paged success", "failed to read pager output files");
        return;
    }
    if (strcmp(got_pty, producer_text) != 0)
        TEST_ERRORF("run_paged success", "pty pager received %s, want %s", got_pty, producer_text);
    if (n_pipe != 0)
        TEST_ERRORF("run_paged success", "pipe pager received %s, want empty", got_pipe);
}

static void test_run_paged_falls_back_to_pipe_pager_on_alloc_failure(void)
{
    const char *producer_text = "paged via pipe fallback\n";

    char pty_out[256];
    char pipe_out[256];
    if (make_tmp_file(pty_out, sizeof(pty_out)) < 0 || make_tmp_file(pipe_out, sizeof(pipe_out)) < 0) {
        TEST_ERRORF("run_paged fallback", "mkstemp failed");
        return;
    }

    char pty_of_arg[sizeof(pty_out) + 8];
    char pipe_of_arg[sizeof(pipe_out) + 8];
    snprintf(pty_of_arg, sizeof(pty_of_arg), "of=%s", pty_out);
    snprintf(pipe_of_arg, sizeof(pipe_of_arg), "of=%s", pipe_out);

    char *primary_argv[] = { "printf", "%s", (char *)producer_text, NULL };
    char *pty_pager_argv[] = { "dd", pty_of_arg, "status=none", NULL };
    char *pipe_pager_argv[] = { "dd", pipe_of_arg, "status=none", NULL };

    run_paged_with_alloc(primary_argv, pty_pager_argv, pipe_pager_argv, failing_pty_alloc);

    char got_pty[4096];
    char got_pipe[4096];
    int n_pty = read_file_contents(pty_out, got_pty, sizeof(got_pty));
    int n_pipe = read_file_contents(pipe_out, got_pipe, sizeof(got_pipe));
    unlink(pty_out);
    unlink(pipe_out);

    if (n_pty < 0 || n_pipe < 0) {
        TEST_ERRORF("run_paged fallback", "failed to read pager output files");
        return;
    }
    if (strcmp(got_pipe, producer_text) != 0)
        TEST_ERRORF("run_paged fallback", "pipe pager received %s, want %s", got_pipe, producer_text);
    if (n_pty != 0)
        TEST_ERRORF("run_paged fallback", "pty pager received %s, want empty", got_pty);
}

static void test_run_via_pty_pager_stdin_is_not_a_tty(void)
{
    const char *producer_text = "hello from producer\n";

    char out_path[256];
    if (make_tmp_file(out_path, sizeof(out_path)) < 0) {
        TEST_ERRORF("pager stdin not a tty", "mkstemp failed");
        return;
    }

    char *primary_argv[] = { "printf", "%s", (char *)producer_text, NULL };
    char *pager_argv[] = { "sh", "-c", "less -R > \"$1\"", "sh", out_path, NULL };

    run_via_pty(primary_argv, pager_argv);

    char got[4096];
    int n = read_file_contents(out_path, got, sizeof(got));
    unlink(out_path);

    if (n < 0) {
        TEST_ERRORF("pager stdin not a tty", "failed to read pager output file");
        return;
    }
    if (strcmp(got, producer_text) != 0)
        TEST_ERRORF("pager stdin not a tty", "less received %s, want %s", got, producer_text);
}

static void test_run_via_pty_producer_receives_forced_term(void)
{
    char out_path[256];
    if (make_tmp_file(out_path, sizeof(out_path)) < 0) {
        TEST_ERRORF("producer receives forced TERM", "mkstemp failed");
        return;
    }

    char of_arg[sizeof(out_path) + 8];
    snprintf(of_arg, sizeof(of_arg), "of=%s", out_path);

    char *primary_argv[] = { "sh", "-c", "printf '%s' \"$TERM\"", NULL };
    char *pager_argv[] = { "dd", of_arg, "status=none", NULL };

    run_via_pty(primary_argv, pager_argv);

    char got[4096];
    int n = read_file_contents(out_path, got, sizeof(got));
    unlink(out_path);

    if (n < 0) {
        TEST_ERRORF("producer receives forced TERM", "failed to read pager output file");
        return;
    }
    if (strcmp(got, "tmux-256color") != 0)
        TEST_ERRORF("producer receives forced TERM", "producer saw TERM=%s, want tmux-256color", got);
}

void test_pager(void)
{
    test_run_via_pty_forwards_producer_output_intact();
    test_run_paged_uses_pty_pager_on_alloc_success();
    test_run_paged_falls_back_to_pipe_pager_on_alloc_failure();
    test_run_via_pty_pager_stdin_is_not_a_tty();
    test_run_via_pty_producer_receives_forced_term();
}

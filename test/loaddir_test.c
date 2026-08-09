#include "minitest.h"
#include "../src/loaddir.h"
#include "../src/msg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_load_directory_excludes_dot_and_dotdot(void)
{
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";

    char tmpl[PATH_MAX_LEN];
    snprintf(tmpl, sizeof(tmpl), "%s/dired_loaddir_test_XXXXXX", tmpdir);
    char *dir = mkdtemp(tmpl);
    if (!dir) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }

    char file_path[PATH_MAX_LEN];
    snprintf(file_path, sizeof(file_path), "%s/real_file.txt", dir);
    FILE *f = fopen(file_path, "w");
    if (f)
        fclose(f);

    Msg msg = load_directory(dir, 0);

    if (msg.type != MSG_DIR_LOADED) {
        TEST_ERRORF("load excludes dot entries", "msg.type = %d, want MSG_DIR_LOADED", msg.type);
        goto cleanup;
    }

    int saw_dot = 0, saw_dotdot = 0, saw_real_file = 0;
    for (int i = 0; i < msg.dir_loaded.entry_count; i++) {
        if (strcmp(msg.dir_loaded.entries[i].name, ".") == 0) saw_dot = 1;
        if (strcmp(msg.dir_loaded.entries[i].name, "..") == 0) saw_dotdot = 1;
        if (strcmp(msg.dir_loaded.entries[i].name, "real_file.txt") == 0) saw_real_file = 1;
    }

    if (saw_dot) {
        TEST_ERRORF("load excludes dot entries", "\".\" present in loaded entries");
    }
    if (saw_dotdot) {
        TEST_ERRORF("load excludes dot entries", "\"..\" present in loaded entries");
    }
    if (!saw_real_file) {
        TEST_ERRORF("load excludes dot entries", "real_file.txt missing from loaded entries");
    }

cleanup:
    unlink(file_path);
    rmdir(dir);
}

static void test_load_directory_missing_path_fails(void)
{
    Msg msg = load_directory("/nonexistent/path/for/dired/tests", 0);

    if (msg.type != MSG_OP_FAILED) {
        TEST_ERRORF("load missing path", "msg.type = %d, want MSG_OP_FAILED", msg.type);
    }
}

static void test_load_directory_show_hidden(void)
{
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";

    char tmpl[PATH_MAX_LEN];
    snprintf(tmpl, sizeof(tmpl), "%s/dired_loaddir_hidden_test_XXXXXX", tmpdir);
    char *dir = mkdtemp(tmpl);
    if (!dir) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }

    char dotfile_path[PATH_MAX_LEN];
    snprintf(dotfile_path, sizeof(dotfile_path), "%s/.env", dir);
    FILE *f = fopen(dotfile_path, "w");
    if (f)
        fclose(f);

    char regular_path[PATH_MAX_LEN];
    snprintf(regular_path, sizeof(regular_path), "%s/real_file.txt", dir);
    f = fopen(regular_path, "w");
    if (f)
        fclose(f);

    typedef struct {
        const char *label;
        int show_hidden;
        int expect_dotfile;
    } Case;

    Case cases[] = {
        {"show_hidden false hides dotfile", 0, 0},
        {"show_hidden true reveals dotfile", 1, 1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Msg msg = load_directory(dir, cases[i].show_hidden);

        if (msg.type != MSG_DIR_LOADED) {
            TEST_ERRORF(cases[i].label, "msg.type = %d, want MSG_DIR_LOADED", msg.type);
            continue;
        }

        int saw_dotfile = 0, saw_real_file = 0;
        for (int j = 0; j < msg.dir_loaded.entry_count; j++) {
            if (strcmp(msg.dir_loaded.entries[j].name, ".env") == 0) saw_dotfile = 1;
            if (strcmp(msg.dir_loaded.entries[j].name, "real_file.txt") == 0) saw_real_file = 1;
        }

        if (saw_dotfile != cases[i].expect_dotfile) {
            TEST_ERRORF(cases[i].label, "saw_dotfile = %d, want %d", saw_dotfile, cases[i].expect_dotfile);
        }
        if (!saw_real_file) {
            TEST_ERRORF(cases[i].label, "real_file.txt missing from loaded entries");
        }
    }

    unlink(dotfile_path);
    unlink(regular_path);
    rmdir(dir);
}

void test_loaddir(void)
{
    test_load_directory_excludes_dot_and_dotdot();
    test_load_directory_missing_path_fails();
    test_load_directory_show_hidden();
}

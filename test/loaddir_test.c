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

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static int run_git(const char *dir, const char *args)
{
    char cmd[PATH_MAX_LEN * 2];
    snprintf(cmd, sizeof(cmd), "git -C %s %s >/dev/null 2>&1", dir, args);
    return system(cmd);
}

static void test_load_directory_classifies_git_status(void)
{
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";

    char tmpl[PATH_MAX_LEN];
    snprintf(tmpl, sizeof(tmpl), "%s/dired_loaddir_git_test_XXXXXX", tmpdir);
    char *dir = mkdtemp(tmpl);
    if (!dir) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }

    run_git(dir, "init -q");
    run_git(dir, "config user.email test@example.com");
    run_git(dir, "config user.name Test");

    char clean_path[PATH_MAX_LEN], tracked_path[PATH_MAX_LEN], gitignore_path[PATH_MAX_LEN];
    snprintf(clean_path, sizeof(clean_path), "%s/clean.txt", dir);
    snprintf(tracked_path, sizeof(tracked_path), "%s/tracked.txt", dir);
    snprintf(gitignore_path, sizeof(gitignore_path), "%s/.gitignore", dir);

    write_file(clean_path, "clean\n");
    write_file(tracked_path, "before\n");
    write_file(gitignore_path, "ignored.txt\n");

    run_git(dir, "add clean.txt tracked.txt .gitignore");
    run_git(dir, "commit -q -m init");

    write_file(tracked_path, "after\n");

    char untracked_path[PATH_MAX_LEN], ignored_path[PATH_MAX_LEN];
    snprintf(untracked_path, sizeof(untracked_path), "%s/untracked.txt", dir);
    snprintf(ignored_path, sizeof(ignored_path), "%s/ignored.txt", dir);
    write_file(untracked_path, "new\n");
    write_file(ignored_path, "ignored\n");

    Msg msg = load_directory(dir, 0);

    if (msg.type != MSG_DIR_LOADED) {
        TEST_ERRORF("git status wiring", "msg.type = %d, want MSG_DIR_LOADED", msg.type);
        goto cleanup;
    }

    GitStatusTag got_clean = GIT_STATUS_NONE, got_tracked = GIT_STATUS_NONE;
    GitStatusTag got_untracked = GIT_STATUS_NONE, got_ignored = GIT_STATUS_NONE;
    int seen_clean = 0, seen_tracked = 0, seen_untracked = 0, seen_ignored = 0;

    for (int i = 0; i < msg.dir_loaded.entry_count; i++) {
        const Entry *e = &msg.dir_loaded.entries[i];
        if (strcmp(e->name, "clean.txt") == 0) { got_clean = e->git_status; seen_clean = 1; }
        if (strcmp(e->name, "tracked.txt") == 0) { got_tracked = e->git_status; seen_tracked = 1; }
        if (strcmp(e->name, "untracked.txt") == 0) { got_untracked = e->git_status; seen_untracked = 1; }
        if (strcmp(e->name, "ignored.txt") == 0) { got_ignored = e->git_status; seen_ignored = 1; }
    }

    if (!seen_clean || !seen_tracked || !seen_untracked || !seen_ignored) {
        TEST_ERRORF("git status wiring", "missing expected entries (clean=%d tracked=%d untracked=%d ignored=%d)",
                     seen_clean, seen_tracked, seen_untracked, seen_ignored);
    }
    if (got_clean != GIT_STATUS_NONE)
        TEST_ERRORF("git status wiring", "clean.txt git_status = %d, want GIT_STATUS_NONE (%d)", got_clean, GIT_STATUS_NONE);
    if (got_tracked != GIT_STATUS_MODIFIED)
        TEST_ERRORF("git status wiring", "tracked.txt git_status = %d, want GIT_STATUS_MODIFIED (%d)", got_tracked, GIT_STATUS_MODIFIED);
    if (got_untracked != GIT_STATUS_UNTRACKED)
        TEST_ERRORF("git status wiring", "untracked.txt git_status = %d, want GIT_STATUS_UNTRACKED (%d)", got_untracked, GIT_STATUS_UNTRACKED);
    if (got_ignored != GIT_STATUS_IGNORED)
        TEST_ERRORF("git status wiring", "ignored.txt git_status = %d, want GIT_STATUS_IGNORED (%d)", got_ignored, GIT_STATUS_IGNORED);

cleanup: ;
    char rmcmd[PATH_MAX_LEN + 16];
    snprintf(rmcmd, sizeof(rmcmd), "rm -rf %s", dir);
    system(rmcmd);
}

static void test_load_directory_non_repo_resets_stale_git_status(void)
{
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";

    char repo_tmpl[PATH_MAX_LEN];
    snprintf(repo_tmpl, sizeof(repo_tmpl), "%s/dired_loaddir_git_repo_XXXXXX", tmpdir);
    char *repo_dir = mkdtemp(repo_tmpl);
    if (!repo_dir) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }

    run_git(repo_dir, "init -q");
    run_git(repo_dir, "config user.email test@example.com");
    run_git(repo_dir, "config user.name Test");

    char dirty_path[PATH_MAX_LEN];
    snprintf(dirty_path, sizeof(dirty_path), "%s/plain.txt", repo_dir);
    write_file(dirty_path, "before\n");
    run_git(repo_dir, "add plain.txt");
    run_git(repo_dir, "commit -q -m init");
    write_file(dirty_path, "after\n");

    Msg first = load_directory(repo_dir, 0);
    if (first.type != MSG_DIR_LOADED) {
        TEST_ERRORF("stale reset", "first load msg.type = %d, want MSG_DIR_LOADED", first.type);
        goto cleanup_repo;
    }

    GitStatusTag first_status = GIT_STATUS_NONE;
    for (int i = 0; i < first.dir_loaded.entry_count; i++) {
        if (strcmp(first.dir_loaded.entries[i].name, "plain.txt") == 0)
            first_status = first.dir_loaded.entries[i].git_status;
    }
    if (first_status != GIT_STATUS_MODIFIED) {
        TEST_ERRORF("stale reset", "plain.txt git_status = %d, want GIT_STATUS_MODIFIED (%d) before reuse check",
                     first_status, GIT_STATUS_MODIFIED);
        goto cleanup_repo;
    }

    char plain_tmpl[PATH_MAX_LEN];
    snprintf(plain_tmpl, sizeof(plain_tmpl), "%s/dired_loaddir_plain_XXXXXX", tmpdir);
    char *plain_dir = mkdtemp(plain_tmpl);
    if (!plain_dir) {
        TEST_ERRORF("setup", "mkdtemp failed");
        goto cleanup_repo;
    }

    char plain_path[PATH_MAX_LEN];
    snprintf(plain_path, sizeof(plain_path), "%s/plain.txt", plain_dir);
    write_file(plain_path, "no repo here\n");

    Msg second = load_directory(plain_dir, 0);
    if (second.type != MSG_DIR_LOADED) {
        TEST_ERRORF("stale reset", "second load msg.type = %d, want MSG_DIR_LOADED", second.type);
        goto cleanup_plain;
    }

    GitStatusTag second_status = GIT_STATUS_CONFLICTED;
    int seen = 0;
    for (int i = 0; i < second.dir_loaded.entry_count; i++) {
        if (strcmp(second.dir_loaded.entries[i].name, "plain.txt") == 0) {
            second_status = second.dir_loaded.entries[i].git_status;
            seen = 1;
        }
    }
    if (!seen) {
        TEST_ERRORF("stale reset", "plain.txt missing from second load");
    } else if (second_status != GIT_STATUS_NONE) {
        TEST_ERRORF("stale reset", "non-repo reload left stale git_status = %d, want GIT_STATUS_NONE (%d)",
                     second_status, GIT_STATUS_NONE);
    }

cleanup_plain: ;
    char rm_plain[PATH_MAX_LEN + 16];
    snprintf(rm_plain, sizeof(rm_plain), "rm -rf %s", plain_dir);
    system(rm_plain);

cleanup_repo: ;
    char rm_repo[PATH_MAX_LEN + 16];
    snprintf(rm_repo, sizeof(rm_repo), "rm -rf %s", repo_dir);
    system(rm_repo);
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
    test_load_directory_classifies_git_status();
    test_load_directory_non_repo_resets_stale_git_status();
}

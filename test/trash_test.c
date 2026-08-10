#include "minitest.h"
#include "../src/trash.h"
#include "../src/msg.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *make_tmpdir(char *out, const char *suffix)
{
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";

    snprintf(out, PATH_MAX_LEN, "%s/dired_trash_test_%s_XXXXXX", tmpdir, suffix);
    return mkdtemp(out);
}

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static void find_trashed_entry(const char *dir, const char *prefix, char *out, size_t out_size)
{
    out[0] = '\0';
    DIR *d = opendir(dir);
    if (!d)
        return;

    struct dirent *de;
    int matches = 0;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, prefix, strlen(prefix)) != 0)
            continue;
        if (strstr(de->d_name, ".trashinfo"))
            continue;
        matches++;
        strncpy(out, de->d_name, out_size - 1);
        out[out_size - 1] = '\0';
    }
    closedir(d);

    if (matches != 1)
        out[0] = '\0';
}

static void test_trash_moves_file_and_writes_metadata(void)
{
    char home_buf[PATH_MAX_LEN], src_dir_buf[PATH_MAX_LEN];
    char *home = make_tmpdir(home_buf, "home");
    char *src_dir = make_tmpdir(src_dir_buf, "src");
    if (!home || !src_dir) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    setenv("HOME", home, 1);

    char src_path[PATH_MAX_LEN];
    snprintf(src_path, sizeof(src_path), "%s/note.txt", src_dir);
    write_file(src_path, "hello");

    Msg msg = trash_item(src_path);

    if (msg.type != MSG_OP_SUCCEEDED) {
        TEST_ERRORF("trash file", "msg.type = %d, want MSG_OP_SUCCEEDED (error=%s)", msg.type, msg.error);
        return;
    }

    struct stat st;
    if (stat(src_path, &st) == 0) {
        TEST_ERRORF("trash file", "original path %s still exists", src_path);
    }

    char trash_dir[PATH_MAX_LEN];
    snprintf(trash_dir, sizeof(trash_dir), "%s/.trash", home);

    char trashed_name[NAME_MAX_LEN + 1];
    find_trashed_entry(trash_dir, "note.txt", trashed_name, sizeof(trashed_name));
    if (trashed_name[0] == '\0') {
        TEST_ERRORF("trash file", "no single note.txt* entry found in %s", trash_dir);
        return;
    }

    char sidecar_path[PATH_MAX_LEN];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(sidecar_path, sizeof(sidecar_path), "%s/%s.trashinfo", trash_dir, trashed_name);
#pragma GCC diagnostic pop
    FILE *f = fopen(sidecar_path, "r");
    if (!f) {
        TEST_ERRORF("trash file", "sidecar %s missing", sidecar_path);
        return;
    }
    char recorded[PATH_MAX_LEN];
    fgets(recorded, sizeof(recorded), f);
    fclose(f);
    recorded[strcspn(recorded, "\n")] = '\0';

    if (strcmp(recorded, src_path) != 0) {
        TEST_ERRORF("trash file", "sidecar records '%s', want '%s'", recorded, src_path);
    }
}

static void test_trash_moves_nonempty_directory(void)
{
    char home_buf[PATH_MAX_LEN], src_dir_buf[PATH_MAX_LEN];
    char *home = make_tmpdir(home_buf, "home2");
    char *src_dir = make_tmpdir(src_dir_buf, "src2");
    if (!home || !src_dir) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    setenv("HOME", home, 1);

    char victim_dir[PATH_MAX_LEN];
    snprintf(victim_dir, sizeof(victim_dir), "%s/project", src_dir);
    mkdir(victim_dir, 0755);
    char nested_file[PATH_MAX_LEN];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(nested_file, sizeof(nested_file), "%s/inside.txt", victim_dir);
#pragma GCC diagnostic pop
    write_file(nested_file, "still here");

    Msg msg = trash_item(victim_dir);

    if (msg.type != MSG_OP_SUCCEEDED) {
        TEST_ERRORF("trash nonempty dir", "msg.type = %d, want MSG_OP_SUCCEEDED (error=%s)", msg.type, msg.error);
        return;
    }

    struct stat st;
    if (stat(victim_dir, &st) == 0) {
        TEST_ERRORF("trash nonempty dir", "original path %s still exists", victim_dir);
    }

    char trash_dir[PATH_MAX_LEN];
    snprintf(trash_dir, sizeof(trash_dir), "%s/.trash", home);
    char trashed_name[NAME_MAX_LEN + 1];
    find_trashed_entry(trash_dir, "project", trashed_name, sizeof(trashed_name));
    if (trashed_name[0] == '\0') {
        TEST_ERRORF("trash nonempty dir", "no single project* entry found in %s", trash_dir);
        return;
    }

    char trashed_nested[PATH_MAX_LEN];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(trashed_nested, sizeof(trashed_nested), "%s/%s/inside.txt", trash_dir, trashed_name);
#pragma GCC diagnostic pop
    if (stat(trashed_nested, &st) != 0) {
        TEST_ERRORF("trash nonempty dir", "nested file %s missing after trash", trashed_nested);
    }
}

static void test_trash_same_name_does_not_collide(void)
{
    char home_buf[PATH_MAX_LEN], src_dir_a_buf[PATH_MAX_LEN], src_dir_b_buf[PATH_MAX_LEN];
    char *home = make_tmpdir(home_buf, "home3");
    char *src_dir_a = make_tmpdir(src_dir_a_buf, "srcA");
    char *src_dir_b = make_tmpdir(src_dir_b_buf, "srcB");
    if (!home || !src_dir_a || !src_dir_b) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    setenv("HOME", home, 1);

    char path_a[PATH_MAX_LEN], path_b[PATH_MAX_LEN];
    snprintf(path_a, sizeof(path_a), "%s/dup.txt", src_dir_a);
    snprintf(path_b, sizeof(path_b), "%s/dup.txt", src_dir_b);
    write_file(path_a, "from a");
    write_file(path_b, "from b");

    Msg msg_a = trash_item(path_a);
    Msg msg_b = trash_item(path_b);

    if (msg_a.type != MSG_OP_SUCCEEDED || msg_b.type != MSG_OP_SUCCEEDED) {
        TEST_ERRORF("no collision", "trashing failed: a=%d(%s) b=%d(%s)",
                    msg_a.type, msg_a.error, msg_b.type, msg_b.error);
        return;
    }

    char trash_dir[PATH_MAX_LEN];
    snprintf(trash_dir, sizeof(trash_dir), "%s/.trash", home);

    DIR *d = opendir(trash_dir);
    int dup_entries = 0;
    struct dirent *de;
    while (d && (de = readdir(d))) {
        if (strncmp(de->d_name, "dup.txt", strlen("dup.txt")) == 0 && !strstr(de->d_name, ".trashinfo"))
            dup_entries++;
    }
    if (d)
        closedir(d);

    if (dup_entries != 2) {
        TEST_ERRORF("no collision", "found %d dup.txt* entries in trash, want 2", dup_entries);
    }
}

void test_trash(void)
{
    test_trash_moves_file_and_writes_metadata();
    test_trash_moves_nonempty_directory();
    test_trash_same_name_does_not_collide();
}

#include "minitest.h"
#include "../src/history.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void assert_state(const char *label, const unsigned char *buf, int n,
                          int want_text_end, int want_index_top, int want_count, int want_idx0)
{
    HistoryArenaState s = history_arena_state(buf, n);
    if (s.text_end != want_text_end)
        TEST_ERRORF(label, "text_end = %d, want %d", s.text_end, want_text_end);
    if (s.index_top != want_index_top)
        TEST_ERRORF(label, "index_top = %d, want %d", s.index_top, want_index_top);
    if (s.count != want_count)
        TEST_ERRORF(label, "count = %d, want %d", s.count, want_count);
    if (s.idx0_offset != want_idx0)
        TEST_ERRORF(label, "idx0_offset = %d, want %d", s.idx0_offset, want_idx0);
}

static void test_arena_worked_example(void)
{
    unsigned char buf[24];
    int n = (int)sizeof(buf);
    history_arena_reset(buf, n);

    history_arena_push(buf, n, "ls");
    history_arena_push(buf, n, "cd ..");
    history_arena_push(buf, n, "pwd");
    assert_state("after three pushes", buf, n, 13, 16, 3, 0);

    history_arena_dedup(buf, n, "cd ..");
    assert_state("after dedup cd ..", buf, n, 13, 16, 3, 0);

    history_arena_record(buf, n, "git");
    assert_state("after record git (forces evict)", buf, n, 14, 16, 3, 6);

    if (strcmp((const char *)buf + 10, "git") != 0)
        TEST_ERRORF("newest text", "buf+10 = '%s', want 'git'", (const char *)buf + 10);
    if (strcmp((const char *)buf + 0, "cd ..") != 0)
        TEST_ERRORF("mid text", "buf+0 = '%s', want 'cd ..'", (const char *)buf + 0);
    if (strcmp((const char *)buf + 6, "pwd") != 0)
        TEST_ERRORF("oldest text", "buf+6 = '%s', want 'pwd'", (const char *)buf + 6);
}

static void test_arena_push_no_room_fails(void)
{
    unsigned char buf[24];
    int n = (int)sizeof(buf);
    history_arena_reset(buf, n);

    int rc = history_arena_push(buf, n, "this command is definitely too long to fit");
    if (rc == 0)
        TEST_ERRORF("oversized push", "expected failure, got success");
}

static void test_arena_dedup_reorders_without_duplicating_text(void)
{
    unsigned char buf[64];
    int n = (int)sizeof(buf);
    history_arena_reset(buf, n);

    history_arena_push(buf, n, "one");
    history_arena_push(buf, n, "two");
    history_arena_push(buf, n, "three");

    HistoryArenaState before = history_arena_state(buf, n);

    history_arena_dedup(buf, n, "one");

    HistoryArenaState after = history_arena_state(buf, n);
    if (after.text_end != before.text_end)
        TEST_ERRORF("dedup text_end", "text_end changed from %d to %d, dedup must not write new text",
                    before.text_end, after.text_end);
    if (after.count != before.count)
        TEST_ERRORF("dedup count", "count changed from %d to %d", before.count, after.count);

    int newest_offset = history_arena_find(buf, n, "one");
    if (strcmp((const char *)buf + newest_offset, "one") != 0)
        TEST_ERRORF("dedup content", "expected 'one' at its offset");

    if (after.idx0_offset == 0 && strcmp((const char *)buf + after.idx0_offset, "one") == 0)
        TEST_ERRORF("dedup idx0", "'one' should no longer be oldest after being re-recorded");
}

static void test_arena_evict_drops_oldest_and_reclaims_space(void)
{
    unsigned char buf[24];
    int n = (int)sizeof(buf);
    history_arena_reset(buf, n);

    history_arena_push(buf, n, "ls");
    history_arena_push(buf, n, "cd ..");
    history_arena_push(buf, n, "pwd");

    int rc = history_arena_evict_oldest(buf, n);
    if (rc != 0)
        TEST_ERRORF("evict", "returned %d, want 0", rc);

    HistoryArenaState s = history_arena_state(buf, n);
    if (s.count != 2)
        TEST_ERRORF("evict count", "count = %d, want 2", s.count);
    if (history_arena_find(buf, n, "ls") != -1)
        TEST_ERRORF("evict removed", "'ls' should have been evicted");
    if (history_arena_find(buf, n, "cd ..") == -1)
        TEST_ERRORF("evict survivor", "'cd ..' should still be present");
    if (history_arena_find(buf, n, "pwd") == -1)
        TEST_ERRORF("evict survivor", "'pwd' should still be present");

    rc = history_arena_push(buf, n, "git");
    if (rc != 0)
        TEST_ERRORF("push after evict", "expected reclaimed space to allow push, got rc=%d", rc);
}

static void test_arena_command_at_indexes_by_recency(void)
{
    unsigned char buf[64];
    int n = (int)sizeof(buf);
    history_arena_reset(buf, n);

    history_arena_push(buf, n, "ls");
    history_arena_push(buf, n, "cd ..");
    history_arena_push(buf, n, "pwd");

    const char *newest = history_arena_command_at(buf, n, 0);
    if (!newest || strcmp(newest, "pwd") != 0)
        TEST_ERRORF("position 0", "expected 'pwd', got '%s'", newest ? newest : "(null)");

    const char *middle = history_arena_command_at(buf, n, 1);
    if (!middle || strcmp(middle, "cd ..") != 0)
        TEST_ERRORF("position 1", "expected 'cd ..', got '%s'", middle ? middle : "(null)");

    const char *oldest = history_arena_command_at(buf, n, 2);
    if (!oldest || strcmp(oldest, "ls") != 0)
        TEST_ERRORF("position 2 (oldest)", "expected 'ls', got '%s'", oldest ? oldest : "(null)");

    if (history_arena_command_at(buf, n, 3) != NULL)
        TEST_ERRORF("out of range", "expected NULL past the oldest entry");
    if (history_arena_command_at(buf, n, -1) != NULL)
        TEST_ERRORF("negative position", "expected NULL for negative position");
}

static void test_arena_command_at_on_empty_arena_is_null(void)
{
    unsigned char buf[64];
    int n = (int)sizeof(buf);
    history_arena_reset(buf, n);

    if (history_arena_command_at(buf, n, 0) != NULL)
        TEST_ERRORF("empty arena", "expected NULL for position 0 with no entries");
}

static void test_folder_history_arena_push_dedup_evict_at_its_own_size(void)
{
    FolderHistoryArena a;
    int n = FOLDER_HISTORY_ARENA_BYTES;
    history_arena_reset(a.data, n);

    history_arena_push(a.data, n, "/home/user/project");
    history_arena_push(a.data, n, "/home/user/other");
    history_arena_push(a.data, n, "/home/user/third");

    HistoryArenaState s = history_arena_state(a.data, n);
    if (s.count != 3)
        TEST_ERRORF("push", "count = %d, want 3", s.count);

    history_arena_dedup(a.data, n, "/home/user/project");
    const char *newest = history_arena_command_at(a.data, n, 0);
    if (!newest || strcmp(newest, "/home/user/project") != 0)
        TEST_ERRORF("dedup", "expected '/home/user/project' newest, got '%s'", newest ? newest : "(null)");

    history_arena_evict_oldest(a.data, n);
    s = history_arena_state(a.data, n);
    if (s.count != 2)
        TEST_ERRORF("evict", "count = %d, want 2", s.count);
    if (history_arena_find(a.data, n, "/home/user/other") != -1)
        TEST_ERRORF("evict", "'/home/user/other' should have been the oldest and evicted");
}

static void test_file_history_arena_push_dedup_evict_at_its_own_size(void)
{
    FileHistoryArena a;
    int n = FILE_HISTORY_ARENA_BYTES;
    history_arena_reset(a.data, n);

    history_arena_push(a.data, n, "/home/user/project/src/main.c");
    history_arena_push(a.data, n, "/home/user/project/src/history.c");
    history_arena_push(a.data, n, "/home/user/project/src/history.h");

    HistoryArenaState s = history_arena_state(a.data, n);
    if (s.count != 3)
        TEST_ERRORF("push", "count = %d, want 3", s.count);

    history_arena_dedup(a.data, n, "/home/user/project/src/main.c");
    const char *newest = history_arena_command_at(a.data, n, 0);
    if (!newest || strcmp(newest, "/home/user/project/src/main.c") != 0)
        TEST_ERRORF("dedup", "expected main.c newest, got '%s'", newest ? newest : "(null)");

    history_arena_evict_oldest(a.data, n);
    s = history_arena_state(a.data, n);
    if (s.count != 2)
        TEST_ERRORF("evict", "count = %d, want 2", s.count);
    if (history_arena_find(a.data, n, "/home/user/project/src/history.c") != -1)
        TEST_ERRORF("evict", "history.c should have been the oldest and evicted");
}

static void test_history_record_lookup_delete(void)
{
    History h = history_create();

    history_record_command(&h, "/home/user/project", "make");
    history_record_command(&h, "/home/user/project", "make test");
    history_record_command(&h, "/home/user/other", "ls -la");

    const CommandArena *a = history_lookup(&h, "/home/user/project");
    if (!a) {
        TEST_ERRORF("lookup", "expected folder entry to exist");
    } else {
        HistoryArenaState s = history_arena_state(a->data, HISTORY_ARENA_BYTES);
        if (s.count != 2)
            TEST_ERRORF("lookup count", "count = %d, want 2", s.count);
    }

    const CommandArena *missing = history_lookup(&h, "/nowhere");
    if (missing != NULL)
        TEST_ERRORF("lookup missing", "expected NULL for folder never recorded");

    history_delete_folder(&h, "/home/user/project");
    if (history_lookup(&h, "/home/user/project") != NULL)
        TEST_ERRORF("delete", "folder entry should be gone after delete");
    if (history_lookup(&h, "/home/user/other") == NULL)
        TEST_ERRORF("delete unrelated", "unrelated folder should survive delete");

    history_free(&h);
}

static void test_folder_enumeration_covers_every_recorded_folder(void)
{
    History h = history_create();

    if (history_folder_count(&h) != 0)
        TEST_ERRORF("empty history", "folder_count = %d, want 0", history_folder_count(&h));
    if (history_folder_path_at(&h, 0) != NULL)
        TEST_ERRORF("empty history", "expected NULL path at index 0");

    history_record_command(&h, "/folder/a", "cmd");
    history_record_command(&h, "/folder/b", "cmd");
    history_record_command(&h, "/folder/c", "cmd");

    if (history_folder_count(&h) != 3)
        TEST_ERRORF("populated history", "folder_count = %d, want 3", history_folder_count(&h));

    int seen_a = 0, seen_b = 0, seen_c = 0;
    for (int i = 0; i < history_folder_count(&h); i++) {
        const char *path = history_folder_path_at(&h, i);
        if (!path)
            TEST_ERRORF("populated history", "unexpected NULL path at index %d", i);
        else if (strcmp(path, "/folder/a") == 0)
            seen_a = 1;
        else if (strcmp(path, "/folder/b") == 0)
            seen_b = 1;
        else if (strcmp(path, "/folder/c") == 0)
            seen_c = 1;
    }
    if (!seen_a || !seen_b || !seen_c)
        TEST_ERRORF("populated history", "expected all three folders to be enumerated");

    if (history_folder_path_at(&h, 3) != NULL)
        TEST_ERRORF("out of range", "expected NULL past the last folder");

    history_free(&h);
}

static char *make_tmpdir(char *out, const char *suffix)
{
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";
    snprintf(out, 512, "%s/dired_history_test_%s_XXXXXX", tmpdir, suffix);
    return mkdtemp(out);
}

static void test_persistence_folder_and_file_history_round_trip(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "picker_sections")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    char file_path[600];
    snprintf(file_path, sizeof(file_path), "%s/history", dir);

    FolderHistoryArena folders;
    history_arena_reset(folders.data, FOLDER_HISTORY_ARENA_BYTES);
    history_arena_push(folders.data, FOLDER_HISTORY_ARENA_BYTES, "/home/user/project");
    history_arena_push(folders.data, FOLDER_HISTORY_ARENA_BYTES, "/home/user/other");

    FileHistoryArena files;
    history_arena_reset(files.data, FILE_HISTORY_ARENA_BYTES);
    history_arena_push(files.data, FILE_HISTORY_ARENA_BYTES, "/home/user/project/src/main.c");

    if (history_write_folder_history(file_path, &folders) != 0)
        TEST_ERRORF("write", "history_write_folder_history failed");
    if (history_write_file_history(file_path, &files) != 0)
        TEST_ERRORF("write", "history_write_file_history failed");

    FolderHistoryArena loaded_folders;
    if (history_load_folder_history(file_path, &loaded_folders) != 0)
        TEST_ERRORF("load", "history_load_folder_history failed");
    if (history_arena_find(loaded_folders.data, FOLDER_HISTORY_ARENA_BYTES, "/home/user/project") == -1)
        TEST_ERRORF("load", "'/home/user/project' missing after round trip");
    if (history_arena_find(loaded_folders.data, FOLDER_HISTORY_ARENA_BYTES, "/home/user/other") == -1)
        TEST_ERRORF("load", "'/home/user/other' missing after round trip");

    FileHistoryArena loaded_files;
    if (history_load_file_history(file_path, &loaded_files) != 0)
        TEST_ERRORF("load", "history_load_file_history failed");
    if (history_arena_find(loaded_files.data, FILE_HISTORY_ARENA_BYTES, "/home/user/project/src/main.c") == -1)
        TEST_ERRORF("load", "main.c missing after round trip");
}

static void test_persistence_folder_and_file_history_survive_folder_slot_writes(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "picker_and_slots")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    char file_path[600];
    snprintf(file_path, sizeof(file_path), "%s/history", dir);

    FolderHistoryArena folders;
    history_arena_reset(folders.data, FOLDER_HISTORY_ARENA_BYTES);
    history_arena_push(folders.data, FOLDER_HISTORY_ARENA_BYTES, "/home/user/project");
    if (history_write_folder_history(file_path, &folders) != 0)
        TEST_ERRORF("write", "history_write_folder_history failed");

    History h = history_create();
    history_record_command(&h, "/home/user/project", "make");
    history_write_folder_slot(file_path, "/home/user/project", history_lookup(&h, "/home/user/project"));

    FolderHistoryArena reloaded;
    if (history_load_folder_history(file_path, &reloaded) != 0)
        TEST_ERRORF("load", "history_load_folder_history failed");
    if (history_arena_find(reloaded.data, FOLDER_HISTORY_ARENA_BYTES, "/home/user/project") == -1)
        TEST_ERRORF("load", "folder-history section should survive a later per-folder command write");

    History loaded;
    history_load_file(file_path, &loaded);
    if (history_lookup(&loaded, "/home/user/project") == NULL)
        TEST_ERRORF("load", "per-folder command data should also be present");

    history_free(&h);
    history_free(&loaded);
}

static void test_persistence_write_then_load_round_trip_single_folder(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "single")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    char file_path[600];
    snprintf(file_path, sizeof(file_path), "%s/history", dir);

    History h = history_create();
    history_record_command(&h, "/home/user/project", "make");
    history_record_command(&h, "/home/user/project", "make test");

    const CommandArena *arena = history_lookup(&h, "/home/user/project");
    if (history_write_folder_slot(file_path, "/home/user/project", arena) != 0) {
        TEST_ERRORF("write", "history_write_folder_slot failed");
    }

    History loaded;
    history_load_file(file_path, &loaded);

    const CommandArena *loaded_arena = history_lookup(&loaded, "/home/user/project");
    if (!loaded_arena) {
        TEST_ERRORF("load", "expected folder to round trip");
    } else {
        HistoryArenaState s = history_arena_state(loaded_arena->data, HISTORY_ARENA_BYTES);
        if (s.count != 2)
            TEST_ERRORF("load count", "count = %d, want 2", s.count);
        if (history_arena_find(loaded_arena->data, HISTORY_ARENA_BYTES, "make") == -1)
            TEST_ERRORF("load content", "'make' missing after round trip");
        if (history_arena_find(loaded_arena->data, HISTORY_ARENA_BYTES, "make test") == -1)
            TEST_ERRORF("load content", "'make test' missing after round trip");
    }

    history_free(&h);
    history_free(&loaded);
}

static void test_persistence_write_then_load_round_trip_multiple_folders(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "multi")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    char file_path[600];
    snprintf(file_path, sizeof(file_path), "%s/history", dir);

    const char *folders[] = { "/home/user/a", "/home/user/b", "/home/user/c" };
    History h = history_create();
    for (int i = 0; i < 3; i++) {
        history_record_command(&h, folders[i], "cmd1");
        const CommandArena *arena = history_lookup(&h, folders[i]);
        history_write_folder_slot(file_path, folders[i], arena);
    }

    History loaded;
    history_load_file(file_path, &loaded);
    for (int i = 0; i < 3; i++) {
        if (history_lookup(&loaded, folders[i]) == NULL)
            TEST_ERRORF(folders[i], "expected folder to be present after multi-folder round trip");
    }

    history_free(&h);
    history_free(&loaded);
}

static void test_persistence_missing_file_means_empty(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "missing")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    char file_path[600];
    snprintf(file_path, sizeof(file_path), "%s/does_not_exist", dir);

    History loaded;
    int rc = history_load_file(file_path, &loaded);
    if (rc != 0)
        TEST_ERRORF("missing file", "history_load_file returned %d, want 0", rc);
    if (history_lookup(&loaded, "/anything") != NULL)
        TEST_ERRORF("missing file", "expected empty history for missing file");

    history_free(&loaded);
}

static void test_persistence_corrupt_magic_means_empty(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "corrupt")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    char file_path[600];
    snprintf(file_path, sizeof(file_path), "%s/history", dir);

    FILE *f = fopen(file_path, "wb");
    if (f) {
        char garbage[64] = "not a history file";
        fwrite(garbage, 1, sizeof(garbage), f);
        fclose(f);
    }

    History loaded;
    int rc = history_load_file(file_path, &loaded);
    if (rc != 0)
        TEST_ERRORF("corrupt file", "history_load_file returned %d, want 0", rc);
    if (history_lookup(&loaded, "/anything") != NULL)
        TEST_ERRORF("corrupt file", "expected empty history for bad magic/version");

    history_free(&loaded);
}

static void test_persistence_incremental_write_does_not_disturb_others(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "incremental")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    char file_path[600];
    snprintf(file_path, sizeof(file_path), "%s/history", dir);

    History h = history_create();
    history_record_command(&h, "/folder/a", "cmd-a");
    history_write_folder_slot(file_path, "/folder/a", history_lookup(&h, "/folder/a"));

    history_record_command(&h, "/folder/b", "cmd-b");
    history_write_folder_slot(file_path, "/folder/b", history_lookup(&h, "/folder/b"));

    history_record_command(&h, "/folder/a", "cmd-a-2");
    history_write_folder_slot(file_path, "/folder/a", history_lookup(&h, "/folder/a"));

    History loaded;
    history_load_file(file_path, &loaded);

    const CommandArena *a = history_lookup(&loaded, "/folder/a");
    const CommandArena *b = history_lookup(&loaded, "/folder/b");
    if (!a || !b) {
        TEST_ERRORF("incremental", "both folders should be present");
    } else {
        HistoryArenaState sa = history_arena_state(a->data, HISTORY_ARENA_BYTES);
        HistoryArenaState sb = history_arena_state(b->data, HISTORY_ARENA_BYTES);
        if (sa.count != 2)
            TEST_ERRORF("incremental", "folder a count = %d, want 2", sa.count);
        if (sb.count != 1)
            TEST_ERRORF("incremental", "folder b count = %d, want 1 (must be untouched by a's rewrite)", sb.count);
    }

    history_free(&h);
    history_free(&loaded);
}

static void test_persistence_file_permissions_0600(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "perms")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    char file_path[600];
    snprintf(file_path, sizeof(file_path), "%s/history", dir);

    History h = history_create();
    history_record_command(&h, "/folder/a", "cmd-a");
    history_write_folder_slot(file_path, "/folder/a", history_lookup(&h, "/folder/a"));

    struct stat st;
    if (stat(file_path, &st) != 0) {
        TEST_ERRORF("permissions", "stat failed on %s", file_path);
    } else {
        mode_t perm_bits = st.st_mode & 0777;
        if (perm_bits != 0600)
            TEST_ERRORF("permissions", "mode = %o, want 0600", perm_bits);
    }

    history_free(&h);
}

static void test_persistence_delete_folder_slot(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "delete")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    char file_path[600];
    snprintf(file_path, sizeof(file_path), "%s/history", dir);

    History h = history_create();
    const char *folders[] = { "/folder/a", "/folder/b", "/folder/c" };
    for (int i = 0; i < 3; i++) {
        history_record_command(&h, folders[i], "cmd");
        history_write_folder_slot(file_path, folders[i], history_lookup(&h, folders[i]));
    }

    if (history_delete_folder_slot(file_path, "/folder/b") != 0)
        TEST_ERRORF("delete", "history_delete_folder_slot failed");

    History loaded;
    history_load_file(file_path, &loaded);

    if (history_lookup(&loaded, "/folder/b") != NULL)
        TEST_ERRORF("delete", "deleted folder should not reappear after reload");
    if (history_lookup(&loaded, "/folder/a") == NULL)
        TEST_ERRORF("delete", "folder a should survive deletion of b");
    if (history_lookup(&loaded, "/folder/c") == NULL)
        TEST_ERRORF("delete", "folder c should survive deletion of b");

    history_free(&h);
    history_free(&loaded);
}

#define TEST_HISTORY_FILE_MAGIC 0x54534968u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t occupied_count;
} TestHistoryFileHeaderV1;

static void write_v1_fixture_file(const char *file_path, const char *folder_path, const char *cmd)
{
    int fd = open(file_path, O_RDWR | O_CREAT, 0600);
    if (fd < 0)
        return;

    unsigned char arena[HISTORY_ARENA_BYTES];
    history_arena_reset(arena, HISTORY_ARENA_BYTES);
    history_arena_push(arena, HISTORY_ARENA_BYTES, cmd);

    TestHistoryFileHeaderV1 hdr = { TEST_HISTORY_FILE_MAGIC, 1u, 1u };
    pwrite(fd, &hdr, sizeof(hdr), 0);

    unsigned char key[HASMMAP_KEY_LEN];
    memset(key, 0, sizeof(key));
    strncpy((char *)key, folder_path, sizeof(key) - 1);

    off_t off = sizeof(hdr);
    pwrite(fd, key, sizeof(key), off);
    pwrite(fd, arena, HISTORY_ARENA_BYTES, off + HASMMAP_KEY_LEN);
    close(fd);
}

static void test_persistence_version1_file_loads_with_folder_data_intact_and_new_arenas_empty(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "v1compat")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    char file_path[600];
    snprintf(file_path, sizeof(file_path), "%s/history", dir);

    write_v1_fixture_file(file_path, "/home/user/legacy", "make");

    History loaded;
    if (history_load_file(file_path, &loaded) != 0)
        TEST_ERRORF("load", "history_load_file failed on v1 fixture");

    const CommandArena *arena = history_lookup(&loaded, "/home/user/legacy");
    if (!arena) {
        TEST_ERRORF("load", "expected legacy folder command data to survive v1 load");
    } else if (history_arena_find(arena->data, HISTORY_ARENA_BYTES, "make") == -1) {
        TEST_ERRORF("load", "'make' missing from legacy folder arena");
    }

    FolderHistoryArena folders;
    if (history_load_folder_history(file_path, &folders) != 0)
        TEST_ERRORF("load", "history_load_folder_history failed on v1 fixture");
    if (history_arena_state(folders.data, FOLDER_HISTORY_ARENA_BYTES).count != 0)
        TEST_ERRORF("load", "expected empty folder history when loading a v1 file");

    FileHistoryArena files;
    if (history_load_file_history(file_path, &files) != 0)
        TEST_ERRORF("load", "history_load_file_history failed on v1 fixture");
    if (history_arena_state(files.data, FILE_HISTORY_ARENA_BYTES).count != 0)
        TEST_ERRORF("load", "expected empty file history when loading a v1 file");

    history_free(&loaded);
}

static void test_persistence_version1_file_upgrades_to_v2_on_next_write(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "v1upgrade")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }
    char file_path[600];
    snprintf(file_path, sizeof(file_path), "%s/history", dir);

    write_v1_fixture_file(file_path, "/home/user/legacy", "make");

    FolderHistoryArena folders;
    history_arena_reset(folders.data, FOLDER_HISTORY_ARENA_BYTES);
    history_arena_push(folders.data, FOLDER_HISTORY_ARENA_BYTES, "/home/user/newly-recorded");
    if (history_write_folder_history(file_path, &folders) != 0)
        TEST_ERRORF("migrate", "history_write_folder_history failed to upgrade v1 file");

    int fd = open(file_path, O_RDONLY);
    uint32_t version = 0;
    if (fd >= 0) {
        pread(fd, &version, sizeof(version), sizeof(uint32_t));
        close(fd);
    }
    if (version != 2u)
        TEST_ERRORF("migrate", "on-disk version = %u, want 2 after upgrading write", version);

    History loaded;
    history_load_file(file_path, &loaded);
    const CommandArena *arena = history_lookup(&loaded, "/home/user/legacy");
    if (!arena || history_arena_find(arena->data, HISTORY_ARENA_BYTES, "make") == -1)
        TEST_ERRORF("migrate", "legacy folder command data lost during v1->v2 upgrade");

    FolderHistoryArena reloaded_folders;
    history_load_folder_history(file_path, &reloaded_folders);
    if (history_arena_find(reloaded_folders.data, FOLDER_HISTORY_ARENA_BYTES, "/home/user/newly-recorded") == -1)
        TEST_ERRORF("migrate", "newly-written folder-history entry missing after upgrade");

    history_free(&loaded);
}

static void test_prune_folder_history_evicts_only_nonexistent_folders(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "prune_folders")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }

    char existing_folder[600];
    snprintf(existing_folder, sizeof(existing_folder), "%s/exists", dir);
    if (mkdir(existing_folder, 0700) != 0)
        TEST_ERRORF("setup", "mkdir failed for fixture folder");

    char plain_file[600];
    snprintf(plain_file, sizeof(plain_file), "%s/plainfile", dir);
    FILE *f = fopen(plain_file, "w");
    if (f)
        fclose(f);

    FolderHistoryArena arena;
    history_arena_reset(arena.data, FOLDER_HISTORY_ARENA_BYTES);
    history_arena_push(arena.data, FOLDER_HISTORY_ARENA_BYTES, existing_folder);
    history_arena_push(arena.data, FOLDER_HISTORY_ARENA_BYTES, "/nonexistent/deleted/folder/xyz");
    history_arena_push(arena.data, FOLDER_HISTORY_ARENA_BYTES, dir);
    history_arena_push(arena.data, FOLDER_HISTORY_ARENA_BYTES, plain_file);

    history_prune_folder_history(&arena);

    HistoryArenaState s = history_arena_state(arena.data, FOLDER_HISTORY_ARENA_BYTES);
    if (s.count != 2)
        TEST_ERRORF("prune", "count = %d, want 2 survivors", s.count);
    if (history_arena_find(arena.data, FOLDER_HISTORY_ARENA_BYTES, existing_folder) == -1)
        TEST_ERRORF("prune", "existing folder should survive pruning");
    if (history_arena_find(arena.data, FOLDER_HISTORY_ARENA_BYTES, dir) == -1)
        TEST_ERRORF("prune", "existing dir should survive pruning");
    if (history_arena_find(arena.data, FOLDER_HISTORY_ARENA_BYTES, "/nonexistent/deleted/folder/xyz") != -1)
        TEST_ERRORF("prune", "nonexistent folder should have been evicted");
    if (history_arena_find(arena.data, FOLDER_HISTORY_ARENA_BYTES, plain_file) != -1)
        TEST_ERRORF("prune", "a plain file should not count as a surviving folder");
}

static void test_prune_file_history_evicts_only_nonexistent_files(void)
{
    char dir[512];
    if (!make_tmpdir(dir, "prune_files")) {
        TEST_ERRORF("setup", "mkdtemp failed");
        return;
    }

    char existing_file[600];
    snprintf(existing_file, sizeof(existing_file), "%s/exists.txt", dir);
    FILE *f = fopen(existing_file, "w");
    if (f)
        fclose(f);

    FileHistoryArena arena;
    history_arena_reset(arena.data, FILE_HISTORY_ARENA_BYTES);
    history_arena_push(arena.data, FILE_HISTORY_ARENA_BYTES, existing_file);
    history_arena_push(arena.data, FILE_HISTORY_ARENA_BYTES, "/nonexistent/deleted/file.txt");

    history_prune_file_history(&arena);

    HistoryArenaState s = history_arena_state(arena.data, FILE_HISTORY_ARENA_BYTES);
    if (s.count != 1)
        TEST_ERRORF("prune", "count = %d, want 1 survivor", s.count);
    if (history_arena_find(arena.data, FILE_HISTORY_ARENA_BYTES, existing_file) == -1)
        TEST_ERRORF("prune", "existing file should survive pruning");
    if (history_arena_find(arena.data, FILE_HISTORY_ARENA_BYTES, "/nonexistent/deleted/file.txt") != -1)
        TEST_ERRORF("prune", "nonexistent file should have been evicted");
}

void test_history(void)
{
    test_arena_worked_example();
    test_arena_push_no_room_fails();
    test_arena_dedup_reorders_without_duplicating_text();
    test_arena_evict_drops_oldest_and_reclaims_space();
    test_arena_command_at_indexes_by_recency();
    test_arena_command_at_on_empty_arena_is_null();
    test_folder_history_arena_push_dedup_evict_at_its_own_size();
    test_file_history_arena_push_dedup_evict_at_its_own_size();
    test_persistence_folder_and_file_history_round_trip();
    test_persistence_folder_and_file_history_survive_folder_slot_writes();
    test_history_record_lookup_delete();
    test_folder_enumeration_covers_every_recorded_folder();
    test_persistence_write_then_load_round_trip_single_folder();
    test_persistence_write_then_load_round_trip_multiple_folders();
    test_persistence_missing_file_means_empty();
    test_persistence_corrupt_magic_means_empty();
    test_persistence_incremental_write_does_not_disturb_others();
    test_persistence_file_permissions_0600();
    test_persistence_delete_folder_slot();
    test_persistence_version1_file_loads_with_folder_data_intact_and_new_arenas_empty();
    test_persistence_version1_file_upgrades_to_v2_on_next_write();
    test_prune_folder_history_evicts_only_nonexistent_folders();
    test_prune_file_history_evicts_only_nonexistent_files();
}

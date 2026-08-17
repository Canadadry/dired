#include "minitest.h"
#include "../src/archive.h"
#include <string.h>

static Entry make_entry(const char *name)
{
    Entry e;
    memset(&e, 0, sizeof(e));
    strcpy(e.name, name);
    return e;
}

static void test_archive_create_base_name(void)
{
    typedef struct {
        ArchiveCreateFormat format;
        const char *expected;
    } Case;

    Case cases[] = {
        {ARCHIVE_CREATE_ZIP, "archive.zip"},
        {ARCHIVE_CREATE_TAR, "archive.tar"},
        {ARCHIVE_CREATE_TARGZ, "archive.tar.gz"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const char *got = archive_create_base_name(cases[i].format);
        if (strcmp(got, cases[i].expected) != 0) {
            TEST_ERRORF(cases[i].expected, "archive_create_base_name(%d) = %s, want %s",
                        cases[i].format, got, cases[i].expected);
        }
    }
}

static void test_archive_create_destination_name(void)
{
    char out[NAME_MAX_LEN + 1];
    archive_create_destination_name(ARCHIVE_CREATE_ZIP, NULL, 0, out, sizeof(out));
    if (strcmp(out, "archive.zip") != 0) {
        TEST_ERRORF("no collision", "archive_create_destination_name(...) = %s, want archive.zip", out);
    }

    Entry entries[1] = { make_entry("archive.zip") };
    archive_create_destination_name(ARCHIVE_CREATE_ZIP, entries, 1, out, sizeof(out));
    if (strcmp(out, "archive (1).zip") != 0) {
        TEST_ERRORF("collision reuses (N) helper",
                    "archive_create_destination_name(...) = %s, want archive (1).zip", out);
    }
}

static void test_archive_extract_subfolder_stem(void)
{
    typedef struct {
        const char *label;
        const char *archive_name;
        int expected_ok;
        const char *expected_stem;
    } Case;

    Case cases[] = {
        {"zip", "something.zip", 1, "something"},
        {"tar", "something.tar", 1, "something"},
        {"tar.gz", "something.tar.gz", 1, "something"},
        {"tgz", "something.tgz", 1, "something"},
        {"tar.bz2", "something.tar.bz2", 1, "something"},
        {"tbz2", "something.tbz2", 1, "something"},
        {"tar.xz", "something.tar.xz", 1, "something"},
        {"txz", "something.txz", 1, "something"},
        {"tar.Z", "something.tar.Z", 1, "something"},
        {"not an archive", "notes.txt", 0, ""},
        {"no extension at all", "notes", 0, ""},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char out[NAME_MAX_LEN + 1];
        int got = archive_extract_subfolder_stem(cases[i].archive_name, out, sizeof(out));

        if (got != cases[i].expected_ok) {
            TEST_ERRORF(cases[i].label, "archive_extract_subfolder_stem(%s) = %d, want %d",
                        cases[i].archive_name, got, cases[i].expected_ok);
            continue;
        }
        if (strcmp(out, cases[i].expected_stem) != 0) {
            TEST_ERRORF(cases[i].label, "archive_extract_subfolder_stem(%s) stem = %s, want %s",
                        cases[i].archive_name, out, cases[i].expected_stem);
        }
    }
}

static void test_archive_extract_destination_name_rejects_non_archive(void)
{
    char out[NAME_MAX_LEN + 1] = "untouched";
    int got = archive_extract_destination_name("notes.txt", NULL, 0, NULL, 0, out, sizeof(out));

    if (got != 0) {
        TEST_ERRORF("non-archive", "archive_extract_destination_name(...) = %d, want 0", got);
    }
    if (out[0] != '\0') {
        TEST_ERRORF("non-archive", "out = %s, want empty string", out);
    }
}

static void test_archive_extract_destination_name_no_collision(void)
{
    char out[NAME_MAX_LEN + 1];
    int got = archive_extract_destination_name("something.zip", NULL, 0, NULL, 0, out, sizeof(out));

    if (got != 1) {
        TEST_ERRORF("no collision", "archive_extract_destination_name(...) = %d, want 1", got);
    }
    if (strcmp(out, "something") != 0) {
        TEST_ERRORF("no collision", "out = %s, want something", out);
    }
}

static void test_archive_extract_destination_name_disk_collision(void)
{
    Entry entries[1] = { make_entry("something") };

    char out[NAME_MAX_LEN + 1];
    int got = archive_extract_destination_name("something.zip", entries, 1, NULL, 0, out, sizeof(out));

    if (got != 1) {
        TEST_ERRORF("disk collision", "archive_extract_destination_name(...) = %d, want 1", got);
    }
    if (strcmp(out, "something (1)") != 0) {
        TEST_ERRORF("disk collision", "out = %s, want something (1)", out);
    }
}

static void test_archive_extract_destination_name_batch_accumulation(void)
{
    char out1[NAME_MAX_LEN + 1];
    int got1 = archive_extract_destination_name("notes.zip", NULL, 0, NULL, 0, out1, sizeof(out1));
    if (got1 != 1 || strcmp(out1, "notes") != 0) {
        TEST_ERRORF("batch first item", "= (%d, %s), want (1, notes)", got1, out1);
    }

    const char *claimed[1] = { out1 };
    char out2[NAME_MAX_LEN + 1];
    int got2 = archive_extract_destination_name("notes.tar.gz", NULL, 0, claimed, 1, out2, sizeof(out2));
    if (got2 != 1 || strcmp(out2, "notes (1)") != 0) {
        TEST_ERRORF("batch second item avoids first item's claimed name",
                    "= (%d, %s), want (1, notes (1))", got2, out2);
    }
}

static void test_parse_tar_listing_single_file(void)
{
    const char *text =
        "-rw-rw-r-- debian/debian     6 2026-08-11 16:58 readme.txt\n";

    ArchiveMember members[16];
    int count = parse_tar_listing(text, members, 16);

    if (count != 1) {
        TEST_ERRORF("single file", "count = %d, want 1", count);
        return;
    }
    if (strcmp(members[0].path, "readme.txt") != 0) {
        TEST_ERRORF("single file", "path = %s, want readme.txt", members[0].path);
    }
    if (members[0].is_dir) {
        TEST_ERRORF("single file", "is_dir = 1, want 0");
    }
    if (members[0].size != 6) {
        TEST_ERRORF("single file", "size = %ld, want 6", (long)members[0].size);
    }
}

static void expect_member(const char *label, const ArchiveMember *members, int count,
                           int index, const char *path, int is_dir, long size)
{
    if (index >= count) {
        TEST_ERRORF(label, "index %d out of range (count = %d)", index, count);
        return;
    }
    if (strcmp(members[index].path, path) != 0) {
        TEST_ERRORF(label, "members[%d].path = %s, want %s", index, members[index].path, path);
    }
    if (members[index].is_dir != is_dir) {
        TEST_ERRORF(label, "members[%d].is_dir = %d, want %d", index, members[index].is_dir, is_dir);
    }
    if (members[index].size != size) {
        TEST_ERRORF(label, "members[%d].size = %ld, want %ld", index,
                     (long)members[index].size, size);
    }
}

static void test_parse_tar_listing_multi_depth_with_explicit_dirs(void)
{
    const char *text =
        "drwxrwxr-x debian/debian     0 2026-08-11 16:58 proj/\n"
        "drwxrwxr-x debian/debian     0 2026-08-11 16:58 proj/assets/\n"
        "-rw-rw-r-- debian/debian     6 2026-08-11 16:58 proj/assets/logo.png\n"
        "-rw-rw-r-- debian/debian     6 2026-08-11 16:58 proj/readme.txt\n"
        "drwxrwxr-x debian/debian     0 2026-08-11 16:58 proj/dir with spaces/\n"
        "-rw-rw-r-- debian/debian     2 2026-08-11 16:58 proj/dir with spaces/file.txt\n"
        "drwxrwxr-x debian/debian     0 2026-08-11 16:58 proj/empty_dir/\n";

    ArchiveMember members[16];
    int count = parse_tar_listing(text, members, 16);

    if (count != 7) {
        TEST_ERRORF("multi depth", "count = %d, want 7", count);
        return;
    }
    expect_member("multi depth", members, count, 0, "proj", 1, 0);
    expect_member("multi depth", members, count, 1, "proj/assets", 1, 0);
    expect_member("multi depth", members, count, 2, "proj/assets/logo.png", 0, 6);
    expect_member("multi depth", members, count, 3, "proj/readme.txt", 0, 6);
    expect_member("multi depth", members, count, 4, "proj/dir with spaces", 1, 0);
    expect_member("multi depth", members, count, 5, "proj/dir with spaces/file.txt", 0, 2);
    expect_member("multi depth", members, count, 6, "proj/empty_dir", 1, 0);
}

static void test_parse_tar_listing_implied_directories(void)
{
    const char *text =
        "-rw-rw-r-- debian/debian     1 2026-08-11 16:59 a/b/c.txt\n";

    ArchiveMember members[16];
    int count = parse_tar_listing(text, members, 16);

    if (count != 1) {
        TEST_ERRORF("implied dirs", "count = %d, want 1", count);
        return;
    }
    expect_member("implied dirs", members, count, 0, "a/b/c.txt", 0, 1);
}

static void test_parse_tar_listing_empty(void)
{
    ArchiveMember members[16];
    int count = parse_tar_listing("", members, 16);

    if (count != 0) {
        TEST_ERRORF("empty", "count = %d, want 0", count);
    }
}

static void test_parse_tar_listing_leading_dot_slash(void)
{
    const char *text =
        "-rw-rw-r-- debian/debian     2 2026-08-11 16:59 ./f.txt\n";

    ArchiveMember members[16];
    int count = parse_tar_listing(text, members, 16);

    if (count != 1) {
        TEST_ERRORF("leading dot slash", "count = %d, want 1", count);
        return;
    }
    expect_member("leading dot slash", members, count, 0, "./f.txt", 0, 2);
}

static void test_parse_zip_listing_single_file(void)
{
    const char *text =
        "Archive:  test.zip\n"
        "  Length      Date    Time    Name\n"
        "---------  ---------- -----   ----\n"
        "        6  2026-08-11 16:58   readme.txt\n"
        "---------                     -------\n"
        "        6                     1 file\n";

    ArchiveMember members[16];
    int count = parse_zip_listing(text, members, 16);

    if (count != 1) {
        TEST_ERRORF("single file", "count = %d, want 1", count);
        return;
    }
    expect_member("single file", members, count, 0, "readme.txt", 0, 6);
}

static void test_parse_zip_listing_multi_depth_with_explicit_dirs(void)
{
    const char *text =
        "Archive:  test.zip\n"
        "  Length      Date    Time    Name\n"
        "---------  ---------- -----   ----\n"
        "        0  2026-08-11 16:58   src/proj/\n"
        "        0  2026-08-11 16:58   src/proj/assets/\n"
        "        6  2026-08-11 16:58   src/proj/assets/logo.png\n"
        "        6  2026-08-11 16:58   src/proj/readme.txt\n"
        "        0  2026-08-11 16:58   src/proj/dir with spaces/\n"
        "        2  2026-08-11 16:58   src/proj/dir with spaces/file.txt\n"
        "        0  2026-08-11 16:58   src/proj/empty_dir/\n"
        "---------                     -------\n"
        "       14                     7 files\n";

    ArchiveMember members[16];
    int count = parse_zip_listing(text, members, 16);

    if (count != 7) {
        TEST_ERRORF("multi depth", "count = %d, want 7", count);
        return;
    }
    expect_member("multi depth", members, count, 0, "src/proj", 1, 0);
    expect_member("multi depth", members, count, 1, "src/proj/assets", 1, 0);
    expect_member("multi depth", members, count, 2, "src/proj/assets/logo.png", 0, 6);
    expect_member("multi depth", members, count, 3, "src/proj/readme.txt", 0, 6);
    expect_member("multi depth", members, count, 4, "src/proj/dir with spaces", 1, 0);
    expect_member("multi depth", members, count, 5, "src/proj/dir with spaces/file.txt", 0, 2);
    expect_member("multi depth", members, count, 6, "src/proj/empty_dir", 1, 0);
}

static void test_parse_zip_listing_implied_directories(void)
{
    const char *text =
        "Archive:  test_implied.zip\n"
        "  Length      Date    Time    Name\n"
        "---------  ---------- -----   ----\n"
        "        1  2026-08-11 16:59   a/b/c.txt\n"
        "---------                     -------\n"
        "        1                     1 file\n";

    ArchiveMember members[16];
    int count = parse_zip_listing(text, members, 16);

    if (count != 1) {
        TEST_ERRORF("implied dirs", "count = %d, want 1", count);
        return;
    }
    expect_member("implied dirs", members, count, 0, "a/b/c.txt", 0, 1);
}

static void test_parse_zip_listing_empty(void)
{
    const char *text =
        "Archive:  empty.zip\n"
        "  Length      Date    Time    Name\n"
        "---------  ---------- -----   ----\n"
        "---------                     -------\n"
        "        0                     0 files\n";

    ArchiveMember members[16];
    int count = parse_zip_listing(text, members, 16);

    if (count != 0) {
        TEST_ERRORF("empty", "count = %d, want 0", count);
    }
}

static ArchiveMember make_member(const char *path, int is_dir, long size)
{
    ArchiveMember m = {0};
    strncpy(m.path, path, sizeof(m.path) - 1);
    m.is_dir = is_dir;
    m.size = size;
    m.mtime = 0;
    return m;
}

static void test_archive_children_at_root(void)
{
    ArchiveMember members[] = {
        make_member("proj", 1, 0),
        make_member("proj/assets", 1, 0),
        make_member("proj/assets/logo.png", 0, 6),
        make_member("proj/readme.txt", 0, 6),
        make_member("docs", 1, 0),
        make_member("docs/readme.md", 0, 10),
        make_member("notes.txt", 0, 3),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    ArchiveMember out[16];
    int count = archive_children_at(members, member_count, "", 1, out, 16);

    if (count != 3) {
        TEST_ERRORF("root", "count = %d, want 3", count);
        return;
    }
    expect_member("root", out, count, 0, "proj", 1, 0);
    expect_member("root", out, count, 1, "docs", 1, 0);
    expect_member("root", out, count, 2, "notes.txt", 0, 3);
}

static void test_archive_children_at_hides_dotfiles_when_show_hidden_false(void)
{
    ArchiveMember members[] = {
        make_member(".git", 1, 0),
        make_member(".gitignore", 0, 10),
        make_member("visible.txt", 0, 5),
        make_member("visible_dir", 1, 0),
        make_member("visible_dir/.hidden_inside", 0, 3),
        make_member("visible_dir/visible_inside.txt", 0, 4),
        make_member(".hidden_dir_implied/inner.txt", 0, 1),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    ArchiveMember root_hidden[16];
    int root_hidden_count = archive_children_at(members, member_count, "", 0, root_hidden, 16);
    if (root_hidden_count != 2) {
        TEST_ERRORF("hides dotfiles at root", "count = %d, want 2", root_hidden_count);
        return;
    }
    expect_member("hides dotfiles at root", root_hidden, root_hidden_count, 0, "visible.txt", 0, 5);
    expect_member("hides dotfiles at root", root_hidden, root_hidden_count, 1, "visible_dir", 1, 0);

    ArchiveMember root_shown[16];
    int root_shown_count = archive_children_at(members, member_count, "", 1, root_shown, 16);
    if (root_shown_count != 5) {
        TEST_ERRORF("shows dotfiles at root", "count = %d, want 5", root_shown_count);
        return;
    }
    expect_member("shows dotfiles at root", root_shown, root_shown_count, 0, ".git", 1, 0);
    expect_member("shows dotfiles at root", root_shown, root_shown_count, 1, ".gitignore", 0, 10);
    expect_member("shows dotfiles at root", root_shown, root_shown_count, 2, "visible.txt", 0, 5);
    expect_member("shows dotfiles at root", root_shown, root_shown_count, 3, "visible_dir", 1, 0);
    expect_member("shows dotfiles at root", root_shown, root_shown_count, 4, ".hidden_dir_implied", 1, 0);

    ArchiveMember nested_hidden[16];
    int nested_hidden_count = archive_children_at(members, member_count, "visible_dir", 0, nested_hidden, 16);
    if (nested_hidden_count != 1) {
        TEST_ERRORF("hides dotfiles at nested subfolder", "count = %d, want 1", nested_hidden_count);
        return;
    }
    expect_member("hides dotfiles at nested subfolder", nested_hidden, nested_hidden_count, 0,
                  "visible_inside.txt", 0, 4);

    ArchiveMember nested_shown[16];
    int nested_shown_count = archive_children_at(members, member_count, "visible_dir", 1, nested_shown, 16);
    if (nested_shown_count != 2) {
        TEST_ERRORF("shows dotfiles at nested subfolder", "count = %d, want 2", nested_shown_count);
        return;
    }
    expect_member("shows dotfiles at nested subfolder", nested_shown, nested_shown_count, 0,
                  ".hidden_inside", 0, 3);
    expect_member("shows dotfiles at nested subfolder", nested_shown, nested_shown_count, 1,
                  "visible_inside.txt", 0, 4);
}

static void test_archive_children_at_nested_subfolder(void)
{
    ArchiveMember members[] = {
        make_member("proj", 1, 0),
        make_member("proj/assets", 1, 0),
        make_member("proj/assets/logo.png", 0, 6),
        make_member("proj/readme.txt", 0, 6),
        make_member("docs", 1, 0),
        make_member("docs/readme.md", 0, 10),
        make_member("notes.txt", 0, 3),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    ArchiveMember out[16];
    int count = archive_children_at(members, member_count, "proj", 1, out, 16);

    if (count != 2) {
        TEST_ERRORF("nested subfolder", "count = %d, want 2", count);
        return;
    }
    expect_member("nested subfolder", out, count, 0, "assets", 1, 0);
    expect_member("nested subfolder", out, count, 1, "readme.txt", 0, 6);
}

static void test_archive_children_at_implied_only(void)
{
    ArchiveMember members[] = {
        make_member("a/b/c.txt", 0, 1),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    ArchiveMember root_out[16];
    int root_count = archive_children_at(members, member_count, "", 1, root_out, 16);
    if (root_count != 1) {
        TEST_ERRORF("implied only root", "count = %d, want 1", root_count);
        return;
    }
    expect_member("implied only root", root_out, root_count, 0, "a", 1, 0);

    ArchiveMember nested_out[16];
    int nested_count = archive_children_at(members, member_count, "a", 1, nested_out, 16);
    if (nested_count != 1) {
        TEST_ERRORF("implied only nested", "count = %d, want 1", nested_count);
        return;
    }
    expect_member("implied only nested", nested_out, nested_count, 0, "b", 1, 0);

    ArchiveMember leaf_out[16];
    int leaf_count = archive_children_at(members, member_count, "a/b", 1, leaf_out, 16);
    if (leaf_count != 1) {
        TEST_ERRORF("implied only leaf", "count = %d, want 1", leaf_count);
        return;
    }
    expect_member("implied only leaf", leaf_out, leaf_count, 0, "c.txt", 0, 1);
}

static void test_archive_children_at_empty_for_childless_subfolder(void)
{
    ArchiveMember members[] = {
        make_member("proj", 1, 0),
        make_member("proj/empty_dir", 1, 0),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    ArchiveMember out[16];
    int count = archive_children_at(members, member_count, "proj/empty_dir", 1, out, 16);

    if (count != 0) {
        TEST_ERRORF("childless subfolder", "count = %d, want 0", count);
    }
}

static void test_archive_glob_matches_at_multiple_depths(void)
{
    ArchiveMember members[] = {
        make_member("readme.txt", 0, 6),
        make_member("src", 1, 0),
        make_member("src/main.c", 0, 42),
        make_member("src/lib", 1, 0),
        make_member("src/lib/util.c", 0, 12),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    ArchiveMember out[16];
    int truncated = 0;
    int count = archive_glob_matches(members, member_count, "", FILTER_PLAIN, ".c",
                                      out, 16, &truncated);

    if (count != 2) {
        TEST_ERRORF("multiple depths", "count = %d, want 2", count);
        return;
    }
    expect_member("multiple depths", out, count, 0, "src/main.c", 0, 42);
    expect_member("multiple depths", out, count, 1, "src/lib/util.c", 0, 12);
    if (truncated) {
        TEST_ERRORF("multiple depths", "truncated = 1, want 0");
    }
}

static void test_archive_glob_matches_implied_directory(void)
{
    ArchiveMember members[] = {
        make_member("a/b/c.txt", 0, 1),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    ArchiveMember out[16];
    int truncated = 0;
    int count = archive_glob_matches(members, member_count, "", FILTER_REGEX, "^a/b$",
                                      out, 16, &truncated);

    if (count != 1) {
        TEST_ERRORF("implied directory", "count = %d, want 1", count);
        return;
    }
    expect_member("implied directory", out, count, 0, "a/b", 1, 0);
}

static void test_archive_glob_matches_no_matches(void)
{
    ArchiveMember members[] = {
        make_member("readme.txt", 0, 6),
        make_member("src", 1, 0),
        make_member("src/main.c", 0, 42),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    ArchiveMember out[16];
    int truncated = 0;
    int count = archive_glob_matches(members, member_count, "", FILTER_PLAIN, "nonexistent",
                                      out, 16, &truncated);

    if (count != 0) {
        TEST_ERRORF("no matches", "count = %d, want 0", count);
    }
    if (truncated) {
        TEST_ERRORF("no matches", "truncated = 1, want 0");
    }
}

static void test_archive_glob_matches_truncates_at_capacity(void)
{
    ArchiveMember members[6];
    for (int i = 0; i < 6; i++) {
        char name[32];
        snprintf(name, sizeof(name), "file%d.txt", i);
        members[i] = make_member(name, 0, i);
    }

    ArchiveMember out[3];
    int truncated = 0;
    int count = archive_glob_matches(members, 6, "", FILTER_PLAIN, ".txt",
                                      out, 3, &truncated);

    if (count != 3) {
        TEST_ERRORF("truncates at capacity", "count = %d, want 3", count);
        return;
    }
    if (!truncated) {
        TEST_ERRORF("truncates at capacity", "truncated = 0, want 1");
    }
}

static void test_archive_glob_matches_relative_to_subfolder(void)
{
    ArchiveMember members[] = {
        make_member("proj", 1, 0),
        make_member("proj/src", 1, 0),
        make_member("proj/src/main.c", 0, 42),
        make_member("proj/readme.txt", 0, 6),
        make_member("other.txt", 0, 3),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    ArchiveMember out[16];
    int truncated = 0;
    int count = archive_glob_matches(members, member_count, "proj", FILTER_PLAIN, "",
                                      out, 16, &truncated);

    if (count != 3) {
        TEST_ERRORF("relative to subfolder", "count = %d, want 3", count);
        return;
    }
    expect_member("relative to subfolder", out, count, 0, "src", 1, 0);
    expect_member("relative to subfolder", out, count, 1, "src/main.c", 0, 42);
    expect_member("relative to subfolder", out, count, 2, "readme.txt", 0, 6);
}

void test_archive(void)
{
    test_archive_create_base_name();
    test_archive_create_destination_name();
    test_archive_extract_subfolder_stem();
    test_archive_extract_destination_name_rejects_non_archive();
    test_archive_extract_destination_name_no_collision();
    test_archive_extract_destination_name_disk_collision();
    test_archive_extract_destination_name_batch_accumulation();

    test_parse_tar_listing_single_file();
    test_parse_tar_listing_multi_depth_with_explicit_dirs();
    test_parse_tar_listing_implied_directories();
    test_parse_tar_listing_empty();
    test_parse_tar_listing_leading_dot_slash();

    test_parse_zip_listing_single_file();
    test_parse_zip_listing_multi_depth_with_explicit_dirs();
    test_parse_zip_listing_implied_directories();
    test_parse_zip_listing_empty();

    test_archive_children_at_root();
    test_archive_children_at_nested_subfolder();
    test_archive_children_at_implied_only();
    test_archive_children_at_empty_for_childless_subfolder();
    test_archive_children_at_hides_dotfiles_when_show_hidden_false();

    test_archive_glob_matches_at_multiple_depths();
    test_archive_glob_matches_implied_directory();
    test_archive_glob_matches_no_matches();
    test_archive_glob_matches_truncates_at_capacity();
    test_archive_glob_matches_relative_to_subfolder();
}

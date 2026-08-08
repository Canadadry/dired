#include "minitest.h"
#include "../src/helpers.h"
#include <string.h>

static void test_is_protected_name(void)
{
    typedef struct {
        const char *name;
        int expected;
    } Case;

    Case cases[] = {
        {".", 1},
        {"..", 1},
        {"a", 0},
        {"...", 0},
        {"", 0},
        {".hidden", 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = is_protected_name(cases[i].name);
        if (got != cases[i].expected) {
            TEST_ERRORF(cases[i].name, "is_protected_name(%s) = %d, want %d",
                        cases[i].name, got, cases[i].expected);
        }
    }
}

static void test_mode_to_str(void)
{
    typedef struct {
        const char *label;
        mode_t mode;
        const char *expected;
    } Case;

    Case cases[] = {
        {"dir rwxr-xr-x", S_IFDIR | 0755, "drwxr-xr-x"},
        {"file rw-r--r--", S_IFREG | 0644, "-rw-r--r--"},
        {"no perms", S_IFREG, "----------"},
        {"all perms", S_IFREG | 0777, "-rwxrwxrwx"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char out[11];
        mode_to_str(cases[i].mode, out);
        if (strcmp(out, cases[i].expected) != 0) {
            TEST_ERRORF(cases[i].label, "mode_to_str = %s, want %s",
                        out, cases[i].expected);
        }
    }
}

static void test_is_binary_content(void)
{
    typedef struct {
        const char *label;
        const unsigned char *buf;
        size_t len;
        int expected;
    } Case;

    unsigned char big_text[512];
    memset(big_text, 'x', sizeof(big_text));

    Case cases[] = {
        {"empty buffer is not binary", (const unsigned char *)"", 0, 0},
        {"all-printable text is not binary", (const unsigned char *)"hello world\n", 12, 0},
        {"null byte at start is binary", (const unsigned char[]){0, 'a', 'b', 'c'}, 4, 1},
        {"null byte in middle is binary", (const unsigned char[]){'a', 'b', 0, 'c', 'd'}, 5, 1},
        {"null byte at end is binary", (const unsigned char[]){'a', 'b', 'c', 0}, 4, 1},
        {"512-byte buffer with no null is not binary", big_text, sizeof(big_text), 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = is_binary_content(cases[i].buf, cases[i].len);
        if (got != cases[i].expected) {
            TEST_ERRORF(cases[i].label, "is_binary_content(...) = %d, want %d",
                        got, cases[i].expected);
        }
    }
}

static Entry make_entry(const char *name)
{
    Entry e;
    memset(&e, 0, sizeof(e));
    strcpy(e.name, name);
    return e;
}

static void test_find_available_name(void)
{
    typedef struct {
        const char *label;
        const char *base_name;
        const char *existing[4];
        int existing_count;
        const char *expected;
    } Case;

    Case cases[] = {
        {"no collision returns base name as-is", "file.txt", {0}, 0, "file.txt"},
        {"one collision inserts (1) before extension", "file.txt",
         {"file.txt"}, 1, "file (1).txt"},
        {"multiple sequential collisions finds first free slot", "file.txt",
         {"file.txt", "file (1).txt"}, 2, "file (2).txt"},
        {"directory has no extension to preserve", "notes",
         {"notes"}, 1, "notes (1)"},
        {"empty entries never collide", "anything", {0}, 0, "anything"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Entry entries[4];
        for (int j = 0; j < cases[i].existing_count; j++)
            entries[j] = make_entry(cases[i].existing[j]);

        char out[NAME_MAX_LEN + 1];
        find_available_name(cases[i].base_name, entries, cases[i].existing_count,
                             out, sizeof(out));

        if (strcmp(out, cases[i].expected) != 0) {
            TEST_ERRORF(cases[i].label, "find_available_name(%s) = %s, want %s",
                        cases[i].base_name, out, cases[i].expected);
        }
    }
}

static void test_classify_new_name(void)
{
    typedef struct {
        const char *label;
        const char *raw;
        NameKind expected_kind;
        const char *expected_name;
    } Case;

    Case cases[] = {
        {"plain name is a file", "notes.txt", NAME_IS_FILE, "notes.txt"},
        {"one trailing slash is a dir", "sub/", NAME_IS_DIR, "sub"},
        {"multiple trailing slashes is a dir", "sub///", NAME_IS_DIR, "sub"},
        {"empty buffer is empty", "", NAME_EMPTY, NULL},
        {"slash-only buffer is empty", "///", NAME_EMPTY, NULL},
        {"embedded non-trailing slash is a file, unchanged", "sub/dir", NAME_IS_FILE, "sub/dir"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char out[NAME_MAX_LEN + 1];
        NameKind got = classify_new_name(cases[i].raw, out, sizeof(out));

        if (got != cases[i].expected_kind) {
            TEST_ERRORF(cases[i].label, "classify_new_name(%s) kind = %d, want %d",
                        cases[i].raw, got, cases[i].expected_kind);
            continue;
        }
        if (cases[i].expected_name && strcmp(out, cases[i].expected_name) != 0) {
            TEST_ERRORF(cases[i].label, "classify_new_name(%s) name = %s, want %s",
                        cases[i].raw, out, cases[i].expected_name);
        }
    }
}

static Entry make_dated_entry(const char *name, mode_t st_mode, off_t size, time_t mtime)
{
    Entry e;
    memset(&e, 0, sizeof(e));
    strcpy(e.name, name);
    e.st.st_mode = st_mode;
    e.st.st_size = size;
    e.st.st_mtime = mtime;
    return e;
}

static void test_entry_compare(void)
{
    typedef struct {
        const char *label;
        Entry a;
        Entry b;
        SortMode sort_mode;
        GroupMode group_mode;
        int expect_sign; /* expected sign of entry_compare(a, b): -1 a-before-b, +1 b-before-a */
    } Case;

    Entry file_a = make_dated_entry("a.txt", S_IFREG | 0644, 100, 1000);
    Entry file_b = make_dated_entry("b.txt", S_IFREG | 0644, 200, 2000);
    Entry dir_c = make_dated_entry("cdir", S_IFDIR | 0755, 4096, 1500);

    Case cases[] = {
        {"name ascending", file_a, file_b, SORT_NAME_ASC, GROUP_MIXED, -1},
        {"name descending", file_a, file_b, SORT_NAME_DESC, GROUP_MIXED, 1},
        {"date ascending", file_a, file_b, SORT_DATE_ASC, GROUP_MIXED, -1},
        {"date descending", file_a, file_b, SORT_DATE_DESC, GROUP_MIXED, 1},
        {"size ascending", file_a, file_b, SORT_SIZE_ASC, GROUP_MIXED, -1},
        {"size descending", file_a, file_b, SORT_SIZE_DESC, GROUP_MIXED, 1},

        {"dirs-first groups dir before file regardless of name", dir_c, file_a, SORT_NAME_ASC, GROUP_DIRS_FIRST, -1},
        {"dirs-last groups dir after file regardless of name", dir_c, file_a, SORT_NAME_ASC, GROUP_DIRS_LAST, 1},
        {"mixed grouping ignores dir/file split", dir_c, file_a, SORT_NAME_ASC, GROUP_MIXED, 1 /* 'a.txt' < 'cdir' */},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int cmp = entry_compare(&cases[i].a, &cases[i].b, cases[i].sort_mode, cases[i].group_mode);
        int got_sign = (cmp > 0) - (cmp < 0);
        if (got_sign != cases[i].expect_sign) {
            TEST_ERRORF(cases[i].label, "entry_compare(a, b) sign = %d, want %d", got_sign, cases[i].expect_sign);
        }
    }

    /* size-tie case above reuses file_a/file_b which don't actually tie on
     * size; build a real tie explicitly. */
    Entry tie_a = make_dated_entry("zzz.txt", S_IFREG | 0644, 100, 1000);
    Entry tie_b = make_dated_entry("aaa.txt", S_IFREG | 0644, 100, 1000);
    int cmp = entry_compare(&tie_a, &tie_b, SORT_SIZE_ASC, GROUP_MIXED);
    if (cmp <= 0) {
        TEST_ERRORF("tie on size breaks by name ascending (real tie)",
                    "entry_compare(zzz, aaa) = %d, want > 0 (aaa before zzz)", cmp);
    }

    /* Extension worked example from the PRD (mixed grouping, ext ascending):
     * zdir/ (dir, no ext), readme (file, no ext), a.md, b.txt
     * -> readme, zdir, a.md, b.txt */
    Entry zdir = make_dated_entry("zdir", S_IFDIR | 0755, 4096, 1000);
    Entry readme = make_dated_entry("readme", S_IFREG | 0644, 10, 1000);
    Entry a_md = make_dated_entry("a.md", S_IFREG | 0644, 10, 1000);
    Entry b_txt = make_dated_entry("b.txt", S_IFREG | 0644, 10, 1000);
    Entry expected_order[] = { readme, zdir, a_md, b_txt };

    for (size_t i = 0; i < 4; i++) {
        for (size_t j = i + 1; j < 4; j++) {
            int c = entry_compare(&expected_order[i], &expected_order[j], SORT_EXT_ASC, GROUP_MIXED);
            if (c >= 0) {
                TEST_ERRORF("extension worked example", "entry_compare(%s, %s) = %d, want < 0",
                            expected_order[i].name, expected_order[j].name, c);
            }
        }
    }
}

void test_helpers(void)
{
    test_is_protected_name();
    test_mode_to_str();
    test_is_binary_content();
    test_find_available_name();
    test_classify_new_name();
    test_entry_compare();
}

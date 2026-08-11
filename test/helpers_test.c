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

static void test_is_hidden_name(void)
{
    typedef struct {
        const char *name;
        int expected;
    } Case;

    Case cases[] = {
        {".hidden", 1},
        {"a", 0},
        {"", 0},
        {".", 1},
        {"..", 1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = is_hidden_name(cases[i].name);
        if (got != cases[i].expected) {
            TEST_ERRORF(cases[i].name, "is_hidden_name(%s) = %d, want %d",
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

static void test_visible_entry_rows(void)
{
    typedef struct {
        const char *label;
        int term_height;
        int has_virtual_line;
        int expected;
    } Case;

    Case cases[] = {
        {"typical terminal, no virtual line", 24, 0, 21},
        {"typical terminal, with virtual line", 24, 1, 20},
        {"short terminal, no virtual line", 10, 0, 7},
        {"minimum chrome-only terminal", 3, 0, 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = visible_entry_rows(cases[i].term_height, cases[i].has_virtual_line);
        if (got != cases[i].expected) {
            TEST_ERRORF(cases[i].label, "visible_entry_rows(%d, %d) = %d, want %d",
                        cases[i].term_height, cases[i].has_virtual_line, got, cases[i].expected);
        }
    }
}

static void test_page_snap_offset(void)
{
    typedef struct {
        const char *label;
        int selected;
        int entry_count;
        int visible_rows;
        int expected;
    } Case;

    Case cases[] = {
        {"exact fit, everything on one page", 5, 10, 10, 0},
        {"more entries than fit, first page", 3, 50, 20, 0},
        {"more entries than fit, crosses into second page", 20, 50, 20, 20},
        {"more entries than fit, last row of a page stays on that page", 19, 50, 20, 0},
        {"fewer entries than fit, single page", 2, 5, 20, 0},
        {"last page shorter than a full page", 44, 45, 20, 40},
        {"selected past entry_count clamps offset to entry_count", 50, 40, 25, 40},
        {"zero visible rows stays at top instead of dividing by zero", 5, 10, 0, 0},
        {"negative visible rows stays at top instead of dividing by zero", 5, 10, -1, 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = page_snap_offset(cases[i].selected, cases[i].entry_count, cases[i].visible_rows);
        if (got != cases[i].expected) {
            TEST_ERRORF(cases[i].label, "page_snap_offset(%d, %d, %d) = %d, want %d",
                        cases[i].selected, cases[i].entry_count, cases[i].visible_rows,
                        got, cases[i].expected);
        }
    }
}

static void test_filter_matches(void)
{
    typedef struct {
        const char *label;
        const char *name;
        FilterType type;
        const char *pattern;
        int expected;
    } Case;

    Case cases[] = {
        {"FILTER_NONE always matches", "anything.txt", FILTER_NONE, "", 1},
        {"plain substring match", "report_final.csv", FILTER_PLAIN, "report", 1},
        {"plain substring no-match", "notes.txt", FILTER_PLAIN, "report", 0},
        {"regex match anchored to extension", "main.c", FILTER_REGEX, "\\.(c|h)$", 1},
        {"regex no-match", "main.py", FILTER_REGEX, "\\.(c|h)$", 0},
        {"regex matches mid-string, unanchored", "myfoo.txt", FILTER_REGEX, "foo", 1},
        {"malformed regex matches nothing", "anything", FILTER_REGEX, "[unterminated", 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = filter_matches(cases[i].name, cases[i].type, cases[i].pattern);
        if (got != cases[i].expected) {
            TEST_ERRORF(cases[i].label, "filter_matches(%s, %d, %s) = %d, want %d",
                        cases[i].name, cases[i].type, cases[i].pattern, got, cases[i].expected);
        }
    }
}

static void test_filter_is_valid(void)
{
    typedef struct {
        const char *label;
        FilterType type;
        const char *pattern;
        int expected;
    } Case;

    Case cases[] = {
        {"plain is always valid regardless of content", FILTER_PLAIN, "[unterminated", 1},
        {"valid regex", FILTER_REGEX, "\\.(c|h)$", 1},
        {"invalid regex, unterminated bracket expression", FILTER_REGEX, "[unterminated", 0},
        {"empty regex is valid", FILTER_REGEX, "", 1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = filter_is_valid(cases[i].type, cases[i].pattern);
        if (got != cases[i].expected) {
            TEST_ERRORF(cases[i].label, "filter_is_valid(%d, %s) = %d, want %d",
                        cases[i].type, cases[i].pattern, got, cases[i].expected);
        }
    }
}

static void test_apply_filter(void)
{
    Entry zeta = make_entry("zeta.txt");
    Entry alpha_report = make_entry("alpha_report.txt");
    Entry middle = make_entry("middle.log");
    Entry beta_report = make_entry("beta_report.csv");
    Entry unfiltered[] = { zeta, alpha_report, middle, beta_report };

    typedef struct {
        const char *label;
        FilterType type;
        const char *pattern;
        int empty_matches_all;
        int expected_count;
        const char *expected_order[4];
    } Case;

    Case cases[] = {
        {"no filter keeps everything, sorted", FILTER_NONE, "", 1,
         4, {"alpha_report.txt", "beta_report.csv", "middle.log", "zeta.txt"}},
        {"plain filter narrows and sorts survivors", FILTER_PLAIN, "report", 1,
         2, {"alpha_report.txt", "beta_report.csv"}},
        {"regex filter narrows and sorts survivors", FILTER_REGEX, "\\.(txt|csv)$", 1,
         3, {"alpha_report.txt", "beta_report.csv", "zeta.txt"}},
        {"pattern matching nothing yields empty output", FILTER_PLAIN, "nonexistent", 1,
         0, {0}},
        {"malformed regex yields empty output", FILTER_REGEX, "[unterminated", 1,
         0, {0}},
        {"empty_matches_all=true, empty plain pattern shows everything", FILTER_PLAIN, "", 1,
         4, {"alpha_report.txt", "beta_report.csv", "middle.log", "zeta.txt"}},
        {"empty_matches_all=false, empty plain pattern shows nothing", FILTER_PLAIN, "", 0,
         0, {0}},
        {"empty_matches_all=false, empty regex pattern shows nothing", FILTER_REGEX, "", 0,
         0, {0}},
        {"empty_matches_all=false, non-empty pattern still matches normally", FILTER_PLAIN, "report", 0,
         2, {"alpha_report.txt", "beta_report.csv"}},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Entry out[4];
        int out_count = -1;
        int out_truncated = -1;
        apply_filter(unfiltered, 4, cases[i].type, cases[i].pattern, cases[i].empty_matches_all,
                     SORT_NAME_ASC, GROUP_MIXED, out, 4, &out_count, &out_truncated);

        if (out_count != cases[i].expected_count) {
            TEST_ERRORF(cases[i].label, "out_count = %d, want %d", out_count, cases[i].expected_count);
            continue;
        }
        if (out_truncated != 0) {
            TEST_ERRORF(cases[i].label, "out_truncated = %d, want 0 (matches fit within capacity)", out_truncated);
        }
        for (int j = 0; j < out_count; j++) {
            if (strcmp(out[j].name, cases[i].expected_order[j]) != 0) {
                TEST_ERRORF(cases[i].label, "out[%d].name = %s, want %s",
                            j, out[j].name, cases[i].expected_order[j]);
            }
        }
    }
}

static void test_apply_filter_truncation(void)
{
    Entry source[6];
    for (int i = 0; i < 6; i++) {
        char name[16];
        snprintf(name, sizeof(name), "file%d.txt", i);
        source[i] = make_entry(name);
    }

    typedef struct {
        const char *label;
        int source_count;
        int capacity;
        int expected_count;
        int expected_truncated;
    } Case;

    Case cases[] = {
        {"matches exceed capacity, output capped and flagged", 6, 4, 4, 1},
        {"matches exactly fill capacity, not truncated", 4, 4, 4, 0},
        {"matches fit under capacity, not truncated", 2, 4, 2, 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Entry out[4];
        int out_count = -1;
        int out_truncated = -1;
        apply_filter(source, cases[i].source_count, FILTER_NONE, "", 1,
                     SORT_NAME_ASC, GROUP_MIXED, out, 4, &out_count, &out_truncated);

        if (out_count != cases[i].expected_count) {
            TEST_ERRORF(cases[i].label, "out_count = %d, want %d", out_count, cases[i].expected_count);
        }
        if (out_truncated != cases[i].expected_truncated) {
            TEST_ERRORF(cases[i].label, "out_truncated = %d, want %d", out_truncated, cases[i].expected_truncated);
        }
    }
}

static size_t build_nul_buf(char *out, const char *segments[], int n)
{
    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        size_t l = strlen(segments[i]);
        memcpy(out + pos, segments[i], l);
        pos += l;
        out[pos++] = '\0';
    }
    return pos;
}

static void test_split_nul_delimited(void)
{
    const char *cwd = "/tmp/globtest";

    typedef struct {
        const char *label;
        const char *segments[4];
        int segment_count;
        int max_count;
        int expected_count;
        int expected_truncated;
        const char *expected_names[4];
    } Case;

    Case cases[] = {
        {"under the cap parses everything, not truncated",
         {"/tmp/globtest/foo.txt", "/tmp/globtest/sub/bar.txt"}, 2, 10,
         2, 0, {"foo.txt", "sub/bar.txt"}},
        {"exactly at the cap is not truncated",
         {"/tmp/globtest/foo.txt", "/tmp/globtest/sub/bar.txt"}, 2, 2,
         2, 0, {"foo.txt", "sub/bar.txt"}},
        {"over the cap parses only the first N and flags truncated",
         {"/tmp/globtest/foo.txt", "/tmp/globtest/sub/bar.txt"}, 2, 1,
         1, 1, {"foo.txt"}},
        {"empty buffer yields zero entries",
         {0}, 0, 10,
         0, 0, {0}},
        {"leading and trailing NUL produce no empty-string entries",
         {"", "/tmp/globtest/foo.txt", ""}, 3, 10,
         1, 0, {"foo.txt"}},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char buf[512];
        size_t len = build_nul_buf(buf, cases[i].segments, cases[i].segment_count);

        Entry out[4];
        int out_count = -1, out_truncated = -1;
        split_nul_delimited(buf, len, cwd, cases[i].max_count, out, &out_count, &out_truncated);

        if (out_count != cases[i].expected_count) {
            TEST_ERRORF(cases[i].label, "out_count = %d, want %d", out_count, cases[i].expected_count);
            continue;
        }
        if (out_truncated != cases[i].expected_truncated) {
            TEST_ERRORF(cases[i].label, "out_truncated = %d, want %d", out_truncated, cases[i].expected_truncated);
        }
        for (int j = 0; j < out_count; j++) {
            if (strcmp(out[j].name, cases[i].expected_names[j]) != 0) {
                TEST_ERRORF(cases[i].label, "out[%d].name = %s, want %s",
                            j, out[j].name, cases[i].expected_names[j]);
            }
        }
    }
}

static void test_dirname_of(void)
{
    typedef struct {
        const char *label;
        const char *name;
        const char *current_path;
        const char *expected;
    } Case;

    Case cases[] = {
        {"bare name returns current_path unchanged", "foo.c", "/home/user", "/home/user"},
        {"one-level nested name", "src/foo.c", "/home/user", "/home/user/src"},
        {"multi-level nested name", "a/b/c.txt", "/home/user", "/home/user/a/b"},
        {"leading-slash-only oddity returns current_path unchanged", "/oddly.txt", "/home/user", "/home/user"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char out[PATH_MAX_LEN];
        dirname_of(cases[i].name, cases[i].current_path, out, sizeof(out));

        if (strcmp(out, cases[i].expected) != 0) {
            TEST_ERRORF(cases[i].label, "dirname_of(%s, %s) = %s, want %s",
                        cases[i].name, cases[i].current_path, out, cases[i].expected);
        }
    }
}

void test_helpers(void)
{
    test_is_protected_name();
    test_is_hidden_name();
    test_mode_to_str();
    test_is_binary_content();
    test_find_available_name();
    test_classify_new_name();
    test_entry_compare();
    test_visible_entry_rows();
    test_page_snap_offset();
    test_filter_matches();
    test_filter_is_valid();
    test_apply_filter();
    test_apply_filter_truncation();
    test_split_nul_delimited();
    test_dirname_of();
}

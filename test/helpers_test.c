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

void test_helpers(void)
{
    test_is_protected_name();
    test_mode_to_str();
    test_is_binary_content();
    test_find_available_name();
}

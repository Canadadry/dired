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

void test_helpers(void)
{
    test_is_protected_name();
    test_mode_to_str();
}

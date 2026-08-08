#include "minitest.h"
#include "../src/model.h"
#include "../src/view.h"
#include <string.h>

static Model make_view_model(void)
{
    Model m;
    memset(&m, 0, sizeof(m));
    m.mode = MODE_NAV;
    strcpy(m.current_path, "/tmp/proj");
    return m;
}

static void set_entry(Model *m, int i, const char *name, mode_t st_mode, long size)
{
    strcpy(m->entries[i].name, name);
    m->entries[i].st.st_mode = st_mode;
    m->entries[i].st.st_size = size;
}

static void test_view_nav_listing(void)
{
    Model m = make_view_model();
    set_entry(&m, 0, "src", S_IFDIR | 0755, 4096);
    set_entry(&m, 1, "main.c", S_IFREG | 0644, 512);
    m.entry_count = 2;
    m.selected = 1;

    View v = view(&m);

    if (v.line_count < 4) {
        TEST_ERRORF("nav listing", "line_count = %d, want at least 4", v.line_count);
        return;
    }
    if (!strstr(v.lines[0].text, "/tmp/proj")) {
        TEST_ERRORF("nav listing", "lines[0] = '%s', want it to mention /tmp/proj", v.lines[0].text);
    }
    if (v.lines[1].text[0] != '\0') {
        TEST_ERRORF("nav listing", "lines[1] = '%s', want empty prompt line in nav mode", v.lines[1].text);
    }
    if (!strstr(v.lines[2].text, "src") || v.lines[2].style != STYLE_NORMAL) {
        TEST_ERRORF("nav listing", "lines[2] = '%s' style=%d, want 'src' STYLE_NORMAL",
                    v.lines[2].text, v.lines[2].style);
    }
    if (!strstr(v.lines[3].text, "main.c") || v.lines[3].style != STYLE_SELECTED) {
        TEST_ERRORF("nav listing", "lines[3] = '%s' style=%d, want 'main.c' STYLE_SELECTED",
                    v.lines[3].text, v.lines[3].style);
    }
    if (v.lines[v.line_count - 1].text[0] == '\0') {
        TEST_ERRORF("nav listing", "last line should be a non-empty help/status bar");
    }
}

static void test_view_create_file_virtual_row(void)
{
    Model m = make_view_model();
    set_entry(&m, 0, "main.c", S_IFREG | 0644, 512);
    m.entry_count = 1;
    m.mode = MODE_CREATE_FILE;
    m.selected = 1;
    strcpy(m.edit_buf, "new.txt");
    m.edit_len = strlen(m.edit_buf);

    View v = view(&m);

    if (strcmp(v.lines[1].text, "Create file:") != 0) {
        TEST_ERRORF("create file", "lines[1] = '%s', want 'Create file:'", v.lines[1].text);
    }
    if (!strstr(v.lines[3].text, "new.txt") || v.lines[3].style != STYLE_SELECTED) {
        TEST_ERRORF("create file", "lines[3] = '%s' style=%d, want 'new.txt' STYLE_SELECTED",
                    v.lines[3].text, v.lines[3].style);
    }
}

static void test_view_rename_keeps_old_name_in_row(void)
{
    Model m = make_view_model();
    set_entry(&m, 0, "old.txt", S_IFREG | 0644, 512);
    m.entry_count = 1;
    m.mode = MODE_RENAME;
    m.selected = 0;
    strcpy(m.edit_buf, "typing-new-name");
    m.edit_len = strlen(m.edit_buf);

    View v = view(&m);

    if (strcmp(v.lines[1].text, "Rename:") != 0) {
        TEST_ERRORF("rename", "lines[1] = '%s', want 'Rename:'", v.lines[1].text);
    }
    if (!strstr(v.lines[2].text, "old.txt")) {
        TEST_ERRORF("rename", "lines[2] = '%s', want it to still show old.txt", v.lines[2].text);
    }
    if (strstr(v.lines[2].text, "typing-new-name")) {
        TEST_ERRORF("rename", "lines[2] = '%s', should not show the in-progress edit buffer", v.lines[2].text);
    }
}

static void test_view_confirm_delete_prompt(void)
{
    Model m = make_view_model();
    set_entry(&m, 0, "old.txt", S_IFREG | 0644, 512);
    m.entry_count = 1;
    m.mode = MODE_CONFIRM_DELETE;
    m.selected = 0;

    View v = view(&m);

    if (!strstr(v.lines[1].text, "old.txt") || v.lines[1].style != STYLE_PROMPT) {
        TEST_ERRORF("confirm delete", "lines[1] = '%s' style=%d, want mention of old.txt, STYLE_PROMPT",
                    v.lines[1].text, v.lines[1].style);
    }
}

static void test_view_yank_pending(void)
{
    Model m = make_view_model();
    set_entry(&m, 0, "main.c", S_IFREG | 0644, 512);
    m.entry_count = 1;
    strcpy(m.yank_path, "/tmp/other/main.c");
    m.yank_is_move = 1;

    View v = view(&m);

    if (!strstr(v.lines[1].text, "main.c") || !strstr(v.lines[1].text, "move")) {
        TEST_ERRORF("yank pending", "lines[1] = '%s', want it to mention main.c and move", v.lines[1].text);
    }
}

static void test_view_no_yank_pending_is_blank(void)
{
    Model m = make_view_model();
    m.entry_count = 0;

    View v = view(&m);

    if (v.lines[1].text[0] != '\0') {
        TEST_ERRORF("no yank pending", "lines[1] = '%s', want empty", v.lines[1].text);
    }
}

static void test_view_error_message(void)
{
    Model m = make_view_model();
    m.mode = MODE_ERROR;
    strcpy(m.error_msg, "Permission denied");

    View v = view(&m);

    if (strcmp(v.lines[1].text, "Permission denied") != 0 || v.lines[1].style != STYLE_ERROR) {
        TEST_ERRORF("error message", "lines[1] = '%s' style=%d, want 'Permission denied' STYLE_ERROR",
                    v.lines[1].text, v.lines[1].style);
    }
}

void test_view(void)
{
    test_view_nav_listing();
    test_view_create_file_virtual_row();
    test_view_rename_keeps_old_name_in_row();
    test_view_confirm_delete_prompt();
    test_view_yank_pending();
    test_view_no_yank_pending_is_blank();
    test_view_error_message();
}

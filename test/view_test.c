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
    m.term_height = 24;
    m.term_width = 80;
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

static void test_view_git_status_style(void)
{
    typedef struct {
        const char *label;
        GitStatusTag git_status;
        int selected;
        StyleTag expected_style;
    } Case;

    Case cases[] = {
        {"conflicted, unselected", GIT_STATUS_CONFLICTED, 0, STYLE_CONFLICTED},
        {"conflicted, selected", GIT_STATUS_CONFLICTED, 1, STYLE_CONFLICTED_SELECTED},
        {"modified, unselected", GIT_STATUS_MODIFIED, 0, STYLE_MODIFIED},
        {"modified, selected", GIT_STATUS_MODIFIED, 1, STYLE_MODIFIED_SELECTED},
        {"untracked, unselected", GIT_STATUS_UNTRACKED, 0, STYLE_UNTRACKED},
        {"untracked, selected", GIT_STATUS_UNTRACKED, 1, STYLE_UNTRACKED_SELECTED},
        {"deleted, unselected", GIT_STATUS_DELETED, 0, STYLE_DELETED},
        {"deleted, selected", GIT_STATUS_DELETED, 1, STYLE_DELETED_SELECTED},
        {"ignored, unselected", GIT_STATUS_IGNORED, 0, STYLE_IGNORED},
        {"ignored, selected", GIT_STATUS_IGNORED, 1, STYLE_IGNORED_SELECTED},
        {"none, unselected", GIT_STATUS_NONE, 0, STYLE_NORMAL},
        {"none, selected", GIT_STATUS_NONE, 1, STYLE_SELECTED},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model m = make_view_model();
        set_entry(&m, 0, "main.c", S_IFREG | 0644, 512);
        m.entries[0].git_status = cases[i].git_status;
        m.entry_count = 1;
        m.selected = cases[i].selected ? 0 : 1;

        View v = view(&m);

        if (v.lines[2].style != cases[i].expected_style) {
            TEST_ERRORF(cases[i].label, "lines[2].style = %d, want %d", v.lines[2].style, cases[i].expected_style);
        }
    }
}

static void test_view_create_virtual_row(void)
{
    Model m = make_view_model();
    set_entry(&m, 0, "main.c", S_IFREG | 0644, 512);
    m.entry_count = 1;
    m.mode = MODE_CREATE;
    m.selected = 1;
    strcpy(m.edit_buf, "new.txt");
    m.edit_len = strlen(m.edit_buf);

    View v = view(&m);

    if (strcmp(v.lines[1].text, "Create:") != 0) {
        TEST_ERRORF("create", "lines[1] = '%s', want 'Create:'", v.lines[1].text);
    }
    if (!strstr(v.lines[3].text, "new.txt") || v.lines[3].style != STYLE_SELECTED) {
        TEST_ERRORF("create", "lines[3] = '%s' style=%d, want 'new.txt' STYLE_SELECTED",
                    v.lines[3].text, v.lines[3].style);
    }
}

static void test_view_run_cmd_echoes_on_prompt_line(void)
{
    Model m = make_view_model();
    set_entry(&m, 0, "main.c", S_IFREG | 0644, 512);
    m.entry_count = 1;
    m.mode = MODE_RUN_CMD;
    m.selected = 0;
    strcpy(m.edit_buf, "!ls -la");
    m.edit_len = strlen(m.edit_buf);

    View v = view(&m);

    if (strcmp(v.lines[1].text, ":!ls -la") != 0) {
        TEST_ERRORF("run cmd", "lines[1] = '%s', want ':!ls -la'", v.lines[1].text);
    }
    if (v.line_count != 4) {
        TEST_ERRORF("run cmd", "line_count = %d, want 4 (path, prompt, 1 entry, help - no virtual row)", v.line_count);
    }
}

static void test_view_filter_echoes_on_prompt_line(void)
{
    typedef struct {
        const char *label;
        FilterType type;
        const char *pattern;
        const char *expected_text;
        StyleTag expected_style;
    } Case;

    Case cases[] = {
        {"plain filter always shows valid style", FILTER_PLAIN, "report", "f:report", STYLE_VALID},
        {"valid regex shows valid style", FILTER_REGEX, "\\.(c|h)$", "F:\\.(c|h)$", STYLE_VALID},
        {"invalid regex shows error style", FILTER_REGEX, "[unterminated", "F:[unterminated", STYLE_ERROR},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model m = make_view_model();
        m.mode = MODE_FILTER;
        m.filter_type = cases[i].type;
        strcpy(m.edit_buf, cases[i].pattern);
        m.edit_len = strlen(cases[i].pattern);

        View v = view(&m);

        if (strcmp(v.lines[1].text, cases[i].expected_text) != 0) {
            TEST_ERRORF(cases[i].label, "lines[1] = '%s', want '%s'", v.lines[1].text, cases[i].expected_text);
        }
        if (v.lines[1].style != cases[i].expected_style) {
            TEST_ERRORF(cases[i].label, "lines[1].style = %d, want %d", v.lines[1].style, cases[i].expected_style);
        }
    }
}

static void test_view_glob_echoes_on_prompt_line(void)
{
    typedef struct {
        const char *label;
        GlobType type;
        const char *pattern;
        const char *expected_text;
        StyleTag expected_style;
    } Case;

    Case cases[] = {
        {"plain glob always shows valid style", GLOB_PLAIN, "report", "g:report", STYLE_VALID},
        {"valid regex shows valid style", GLOB_REGEX, "\\.(c|h)$", "G:\\.(c|h)$", STYLE_VALID},
        {"invalid regex shows error style", GLOB_REGEX, "[unterminated", "G:[unterminated", STYLE_ERROR},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model m = make_view_model();
        m.mode = MODE_GLOB;
        m.glob_type = cases[i].type;
        strcpy(m.edit_buf, cases[i].pattern);
        m.edit_len = strlen(cases[i].pattern);

        View v = view(&m);

        if (strcmp(v.lines[1].text, cases[i].expected_text) != 0) {
            TEST_ERRORF(cases[i].label, "lines[1] = '%s', want '%s'", v.lines[1].text, cases[i].expected_text);
        }
        if (v.lines[1].style != cases[i].expected_style) {
            TEST_ERRORF(cases[i].label, "lines[1].style = %d, want %d", v.lines[1].style, cases[i].expected_style);
        }
    }
}

static void test_view_glob_status_shown_when_active_and_no_yank(void)
{
    Model m = make_view_model();
    m.glob_type = GLOB_PLAIN;
    strcpy(m.glob_pattern, "report");

    View v = view(&m);

    if (!strstr(v.lines[1].text, "report")) {
        TEST_ERRORF("glob status", "lines[1] = '%s', want it to mention the active pattern 'report'", v.lines[1].text);
    }
}

static void test_view_glob_status_shows_capped_hint(void)
{
    Model m = make_view_model();
    m.glob_type = GLOB_PLAIN;
    strcpy(m.glob_pattern, "report");
    m.glob_capped = 1;

    View v = view(&m);

    if (!strstr(v.lines[1].text, "report") || !strstr(v.lines[1].text, "1024")) {
        TEST_ERRORF("glob capped hint", "lines[1] = '%s', want it to mention 'report' and the 1024 cap", v.lines[1].text);
    }
}

static void test_view_glob_status_no_hint_when_not_capped(void)
{
    Model m = make_view_model();
    m.glob_type = GLOB_PLAIN;
    strcpy(m.glob_pattern, "report");
    m.glob_capped = 0;

    View v = view(&m);

    if (strstr(v.lines[1].text, "1024")) {
        TEST_ERRORF("glob no capped hint", "lines[1] = '%s', should not mention the cap", v.lines[1].text);
    }
}

static void test_view_yank_status_takes_priority_over_glob(void)
{
    Model m = make_view_model();
    set_entry(&m, 0, "main.c", S_IFREG | 0644, 512);
    m.entry_count = 1;
    strcpy(m.yank_paths[0], "/tmp/other/main.c");
    m.yank_count = 1;
    m.yank_is_move = 1;
    m.glob_type = GLOB_PLAIN;
    strcpy(m.glob_pattern, "report");

    View v = view(&m);

    if (!strstr(v.lines[1].text, "main.c") || !strstr(v.lines[1].text, "move")) {
        TEST_ERRORF("yank priority", "lines[1] = '%s', want yank status, not glob status", v.lines[1].text);
    }
    if (strstr(v.lines[1].text, "report")) {
        TEST_ERRORF("yank priority", "lines[1] = '%s', should not mention glob pattern while yank is pending", v.lines[1].text);
    }
}

static void test_view_filter_status_shown_when_active_and_no_yank(void)
{
    Model m = make_view_model();
    m.filter_type = FILTER_PLAIN;
    strcpy(m.filter_pattern, "report");

    View v = view(&m);

    if (!strstr(v.lines[1].text, "report")) {
        TEST_ERRORF("filter status", "lines[1] = '%s', want it to mention the active pattern 'report'", v.lines[1].text);
    }
}

static void test_view_yank_status_takes_priority_over_filter(void)
{
    Model m = make_view_model();
    set_entry(&m, 0, "main.c", S_IFREG | 0644, 512);
    m.entry_count = 1;
    strcpy(m.yank_paths[0], "/tmp/other/main.c");
    m.yank_count = 1;
    m.yank_is_move = 1;
    m.filter_type = FILTER_PLAIN;
    strcpy(m.filter_pattern, "report");

    View v = view(&m);

    if (!strstr(v.lines[1].text, "main.c") || !strstr(v.lines[1].text, "move")) {
        TEST_ERRORF("yank priority", "lines[1] = '%s', want yank status, not filter status", v.lines[1].text);
    }
    if (strstr(v.lines[1].text, "report")) {
        TEST_ERRORF("yank priority", "lines[1] = '%s', should not mention filter pattern while yank is pending", v.lines[1].text);
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
    typedef struct {
        const char *label;
        int confirm_permanent_delete;
        const char *expected_substr;
    } Case;

    Case cases[] = {
        {"trash wording", 0, "to trash"},
        {"permanent delete wording", 1, "Delete 'old.txt'"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model m = make_view_model();
        set_entry(&m, 0, "old.txt", S_IFREG | 0644, 512);
        m.entry_count = 1;
        m.mode = MODE_CONFIRM_DELETE;
        m.selected = 0;
        m.confirm_permanent_delete = cases[i].confirm_permanent_delete;

        View v = view(&m);

        if (!strstr(v.lines[1].text, "old.txt") || v.lines[1].style != STYLE_PROMPT ||
            !strstr(v.lines[1].text, cases[i].expected_substr)) {
            TEST_ERRORF(cases[i].label, "lines[1] = '%s' style=%d, want mention of old.txt and '%s', STYLE_PROMPT",
                        v.lines[1].text, v.lines[1].style, cases[i].expected_substr);
        }
    }
}

static void test_view_yank_pending(void)
{
    Model m = make_view_model();
    set_entry(&m, 0, "main.c", S_IFREG | 0644, 512);
    m.entry_count = 1;
    strcpy(m.yank_paths[0], "/tmp/other/main.c");
    m.yank_count = 1;
    m.yank_is_move = 1;

    View v = view(&m);

    if (!strstr(v.lines[1].text, "main.c") || !strstr(v.lines[1].text, "move")) {
        TEST_ERRORF("yank pending", "lines[1] = '%s', want it to mention main.c and move", v.lines[1].text);
    }
}

static void test_view_yank_pending_batch_shows_count(void)
{
    Model m = make_view_model();
    set_entry(&m, 0, "main.c", S_IFREG | 0644, 512);
    m.entry_count = 1;
    strcpy(m.yank_paths[0], "/tmp/other/main.c");
    strcpy(m.yank_paths[1], "/tmp/other/util.c");
    strcpy(m.yank_paths[2], "/tmp/other/util.h");
    m.yank_count = 3;
    m.yank_is_move = 0;

    View v = view(&m);

    if (!strstr(v.lines[1].text, "3") || !strstr(v.lines[1].text, "items") || !strstr(v.lines[1].text, "copy")) {
        TEST_ERRORF("yank pending batch", "lines[1] = '%s', want it to mention 3 items and copy", v.lines[1].text);
    }
    if (strstr(v.lines[1].text, "main.c")) {
        TEST_ERRORF("yank pending batch", "lines[1] = '%s', should not name an individual file for a batch yank",
                    v.lines[1].text);
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

static void test_view_paginates_long_list(void)
{
    Model m = make_view_model();
    m.entry_count = 25;
    for (int i = 0; i < m.entry_count; i++) {
        char name[16];
        snprintf(name, sizeof(name), "file%d", i);
        set_entry(&m, i, name, S_IFREG | 0644, 100);
    }
    m.selected = 0;
    m.scroll_offset = 0;

    View v = view(&m);

    int entry_lines = v.line_count - 3;
    if (entry_lines != 21) {
        TEST_ERRORF("paginate long list", "entry line count = %d, want 21", entry_lines);
        return;
    }
    for (int i = 0; i < 21; i++) {
        char name[16];
        snprintf(name, sizeof(name), "file%d", i);
        if (!strstr(v.lines[2 + i].text, name)) {
            TEST_ERRORF("paginate long list", "lines[%d] = '%s', want it to contain '%s'",
                        2 + i, v.lines[2 + i].text, name);
        }
    }
    for (int i = 21; i < 25; i++) {
        char name[16];
        snprintf(name, sizeof(name), "file%d", i);
        for (int l = 0; l < v.line_count; l++) {
            if (strstr(v.lines[l].text, name)) {
                TEST_ERRORF("paginate long list", "lines[%d] = '%s' should not contain off-page '%s'",
                            l, v.lines[l].text, name);
            }
        }
    }
}

static int ends_with_marker(const char *text, int term_width, const char *marker)
{
    size_t marker_len = strlen(marker);
    size_t len = strlen(text);
    if (len != (size_t)term_width)
        return 0;
    return strcmp(text + (term_width - (int)marker_len), marker) == 0;
}

static void set_paged_entries(Model *m, int count)
{
    m->entry_count = count;
    for (int i = 0; i < count; i++) {
        char name[16];
        snprintf(name, sizeof(name), "file%d", i);
        set_entry(m, i, name, S_IFREG | 0644, 100);
    }
}

static void assert_no_entry_row_has_marker(View *v, const char *label)
{
    for (int i = 2; i < v->line_count - 1; i++) {
        if (strstr(v->lines[i].text, "<-") || strstr(v->lines[i].text, "->")) {
            TEST_ERRORF(label, "lines[%d] = '%s', want no marker on an entry/virtual row", i, v->lines[i].text);
        }
    }
}

static void test_view_page_indicator_first_page(void)
{
    Model m = make_view_model();
    set_paged_entries(&m, 50);
    m.scroll_offset = 0;

    View v = view(&m);

    if (!ends_with_marker(v.lines[1].text, m.term_width, "->")) {
        TEST_ERRORF("first page", "status line = '%s', want it padded to %d cols and end with '->'",
                    v.lines[1].text, m.term_width);
    }
    assert_no_entry_row_has_marker(&v, "first page");
}

static void test_view_page_indicator_middle_page(void)
{
    Model m = make_view_model();
    set_paged_entries(&m, 50);
    m.scroll_offset = 21;

    View v = view(&m);

    if (!ends_with_marker(v.lines[1].text, m.term_width, "<- ->")) {
        TEST_ERRORF("middle page", "status line = '%s', want it padded to %d cols and end with '<- ->'",
                    v.lines[1].text, m.term_width);
    }
    assert_no_entry_row_has_marker(&v, "middle page");
}

static void test_view_page_indicator_last_page(void)
{
    Model m = make_view_model();
    set_paged_entries(&m, 50);
    m.scroll_offset = 42;

    View v = view(&m);

    if (!ends_with_marker(v.lines[1].text, m.term_width, "<-")) {
        TEST_ERRORF("last page", "status line = '%s', want it padded to %d cols and end with '<-'",
                    v.lines[1].text, m.term_width);
    }
    if (strstr(v.lines[1].text, "->")) {
        TEST_ERRORF("last page", "status line = '%s', want no '->' (nothing more below)", v.lines[1].text);
    }
    assert_no_entry_row_has_marker(&v, "last page");
}

static void test_view_page_indicator_survives_long_status_text(void)
{
    Model m = make_view_model();
    set_paged_entries(&m, 50);
    m.scroll_offset = 21;

    char long_path[101];
    memset(long_path, 'x', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    strcpy(m.yank_paths[0], long_path);
    m.yank_count = 1;
    m.yank_is_move = 0;

    View v = view(&m);

    if (!ends_with_marker(v.lines[1].text, m.term_width, "<- ->")) {
        TEST_ERRORF("long status text", "status line = '%s' (len %zu), want it truncated to %d cols and end with '<- ->'",
                    v.lines[1].text, strlen(v.lines[1].text), m.term_width);
    }
}

static void test_view_no_page_indicator_when_everything_fits(void)
{
    Model m = make_view_model();
    set_paged_entries(&m, 5);

    View v = view(&m);

    if (strstr(v.lines[1].text, "<-") || strstr(v.lines[1].text, "->")) {
        TEST_ERRORF("no indicator", "status line = '%s', want no marker (everything fits)", v.lines[1].text);
    }
}

static void test_view_page_indicator_during_create_mode(void)
{
    Model m = make_view_model();
    set_paged_entries(&m, 50);
    m.mode = MODE_CREATE;
    m.scroll_offset = 21;
    m.selected = m.entry_count;
    strcpy(m.edit_buf, "new.txt");
    m.edit_len = strlen(m.edit_buf);

    View v = view(&m);

    if (!ends_with_marker(v.lines[1].text, m.term_width, "<- ->")) {
        TEST_ERRORF("create mode indicator", "status line = '%s', want it padded to %d cols and end with '<- ->'",
                    v.lines[1].text, m.term_width);
    }
    if (strncmp(v.lines[1].text, "Create:", strlen("Create:")) != 0) {
        TEST_ERRORF("create mode indicator", "status line = '%s', want it to still start with 'Create:'", v.lines[1].text);
    }
    assert_no_entry_row_has_marker(&v, "create mode indicator");
}

void test_view(void)
{
    test_view_nav_listing();
    test_view_git_status_style();
    test_view_paginates_long_list();
    test_view_page_indicator_first_page();
    test_view_page_indicator_middle_page();
    test_view_page_indicator_last_page();
    test_view_page_indicator_survives_long_status_text();
    test_view_no_page_indicator_when_everything_fits();
    test_view_page_indicator_during_create_mode();
    test_view_create_virtual_row();
    test_view_run_cmd_echoes_on_prompt_line();
    test_view_filter_echoes_on_prompt_line();
    test_view_filter_status_shown_when_active_and_no_yank();
    test_view_yank_status_takes_priority_over_filter();
    test_view_glob_echoes_on_prompt_line();
    test_view_glob_status_shown_when_active_and_no_yank();
    test_view_glob_status_shows_capped_hint();
    test_view_glob_status_no_hint_when_not_capped();
    test_view_yank_status_takes_priority_over_glob();
    test_view_rename_keeps_old_name_in_row();
    test_view_confirm_delete_prompt();
    test_view_yank_pending();
    test_view_yank_pending_batch_shows_count();
    test_view_no_yank_pending_is_blank();
    test_view_error_message();
}

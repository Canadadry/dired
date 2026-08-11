#include "minitest.h"
#include "../src/model.h"
#include "../src/msg.h"
#include "../src/cmd.h"
#include "../src/update.h"
#include "../src/helpers.h"
#include <string.h>

static Model make_nav_model(int entry_count, int selected)
{
    Model m;
    memset(&m, 0, sizeof(m));
    m.mode = MODE_NAV;
    m.entry_count = entry_count;
    m.selected = selected;
    m.term_height = 24;
    m.term_width = 80;
    strcpy(m.current_path, "/tmp");
    return m;
}

static void test_move_selection(void)
{
    typedef struct {
        const char *label;
        int entry_count;
        int selected;
        MsgType msg_type;
        int expected_selected;
    } Case;

    Case cases[] = {
        {"up from middle", 5, 2, MSG_MOVE_UP, 1},
        {"up clamped at top", 5, 0, MSG_MOVE_UP, 0},
        {"down from middle", 5, 2, MSG_MOVE_DOWN, 3},
        {"down clamped at bottom", 5, 4, MSG_MOVE_DOWN, 4},
        {"down on empty dir stays put", 0, 0, MSG_MOVE_DOWN, 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(cases[i].entry_count, cases[i].selected);
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.selected != cases[i].expected_selected) {
            TEST_ERRORF(cases[i].label, "selected = %d, want %d",
                        out.selected, cases[i].expected_selected);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static void test_go_parent(void)
{
    typedef struct {
        const char *current_path;
        const char *expected_parent;
    } Case;

    Case cases[] = {
        {"/home/user/project", "/home/user"},
        {"/home/user", "/home"},
        {"/home", "/"},
        {"/", "/"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(0, 0);
        strcpy(in.current_path, cases[i].current_path);
        in.show_hidden = 1;
        Msg msg = { .type = MSG_GO_PARENT };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != CMD_LOAD_DIR) {
            TEST_ERRORF(cases[i].current_path, "cmd.type = %d, want CMD_LOAD_DIR", cmd.type);
        }
        if (strcmp(cmd.path, cases[i].expected_parent) != 0) {
            TEST_ERRORF(cases[i].current_path, "cmd.path = %s, want %s",
                        cmd.path, cases[i].expected_parent);
        }
        if (!cmd.show_hidden) {
            TEST_ERRORF(cases[i].current_path, "cmd.show_hidden = %d, want 1 (carried from model)", cmd.show_hidden);
        }
    }
}

static void test_go_parent_resets_active_filter(void)
{
    Model in = make_nav_model(0, 0);
    strcpy(in.current_path, "/home/user/project");
    in.filter_type = FILTER_PLAIN;
    strcpy(in.filter_pattern, "report");
    Msg msg = { .type = MSG_GO_PARENT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.filter_type != FILTER_NONE || out.filter_pattern[0] != '\0') {
        TEST_ERRORF("go parent resets filter", "filter = {%d, '%s'}, want {FILTER_NONE, ''}",
                    out.filter_type, out.filter_pattern);
    }
    if (cmd.type != CMD_LOAD_DIR) {
        TEST_ERRORF("go parent resets filter", "cmd.type = %d, want CMD_LOAD_DIR", cmd.type);
    }
}

static void test_go_parent_resets_active_glob(void)
{
    Model in = make_nav_model(0, 0);
    strcpy(in.current_path, "/home/user/project");
    in.glob_type = GLOB_PLAIN;
    strcpy(in.glob_pattern, "report");
    Msg msg = { .type = MSG_GO_PARENT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.glob_type != GLOB_NONE || out.glob_pattern[0] != '\0') {
        TEST_ERRORF("go parent resets glob", "glob = {%d, '%s'}, want {GLOB_NONE, ''}",
                    out.glob_type, out.glob_pattern);
    }
    if (cmd.type != CMD_LOAD_DIR) {
        TEST_ERRORF("go parent resets glob", "cmd.type = %d, want CMD_LOAD_DIR", cmd.type);
    }
}

static Model make_model_with_entry(const char *current_path, const char *name, mode_t st_mode, int selected)
{
    Model m = make_nav_model(1, selected);
    strcpy(m.current_path, current_path);
    strcpy(m.entries[0].name, name);
    m.entries[0].st.st_mode = st_mode;
    return m;
}

static void test_activate(void)
{
    typedef struct {
        const char *label;
        const char *current_path;
        const char *name;
        mode_t st_mode;
        int entry_count;
        int selected;
        CmdType expected_cmd_type;
        const char *expected_cmd_path;
    } Case;

    Case cases[] = {
        {"open directory", "/home/user", "sub", S_IFDIR | 0755, 1, 0,
         CMD_LOAD_DIR, "/home/user/sub"},
        {"open file", "/home/user", "main.c", S_IFREG | 0644, 1, 0,
         CMD_LAUNCH_EDITOR, "/home/user/main.c"},
        {"protected dot entry is a no-op", "/home/user", ".", S_IFDIR | 0755, 1, 0,
         CMD_NONE, NULL},
        {"protected dotdot entry is a no-op", "/home/user", "..", S_IFDIR | 0755, 1, 0,
         CMD_NONE, NULL},
        {"empty directory is a no-op", "/home/user", "", 0, 0, 0,
         CMD_NONE, NULL},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_model_with_entry(cases[i].current_path, cases[i].name,
                                          cases[i].st_mode, cases[i].selected);
        in.entry_count = cases[i].entry_count;
        in.show_hidden = 1;
        Msg msg = { .type = MSG_ACTIVATE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != cases[i].expected_cmd_type) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want %d", cmd.type, cases[i].expected_cmd_type);
            continue;
        }
        if (cases[i].expected_cmd_path && strcmp(cmd.path, cases[i].expected_cmd_path) != 0) {
            TEST_ERRORF(cases[i].label, "cmd.path = %s, want %s", cmd.path, cases[i].expected_cmd_path);
        }
        if (cases[i].expected_cmd_type == CMD_LOAD_DIR && !cmd.show_hidden) {
            TEST_ERRORF(cases[i].label, "cmd.show_hidden = %d, want 1 (carried from model)", cmd.show_hidden);
        }
    }
}

static void test_activate_into_directory_resets_filter_but_opening_a_file_does_not(void)
{
    typedef struct {
        const char *label;
        const char *name;
        mode_t st_mode;
        FilterType expected_type;
        const char *expected_pattern;
    } Case;

    Case cases[] = {
        {"activating into a directory resets the filter", "sub", S_IFDIR | 0755, FILTER_NONE, ""},
        {"opening a file leaves an active filter untouched", "main.c", S_IFREG | 0644, FILTER_PLAIN, "report"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_model_with_entry("/home/user", cases[i].name, cases[i].st_mode, 0);
        in.filter_type = FILTER_PLAIN;
        strcpy(in.filter_pattern, "report");
        Msg msg = { .type = MSG_ACTIVATE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.filter_type != cases[i].expected_type || strcmp(out.filter_pattern, cases[i].expected_pattern) != 0) {
            TEST_ERRORF(cases[i].label, "filter = {%d, '%s'}, want {%d, '%s'}",
                        out.filter_type, out.filter_pattern, cases[i].expected_type, cases[i].expected_pattern);
        }
    }
}

static void test_activate_into_directory_resets_active_glob_but_opening_a_file_does_not(void)
{
    typedef struct {
        const char *label;
        const char *name;
        mode_t st_mode;
        GlobType expected_type;
        const char *expected_pattern;
    } Case;

    Case cases[] = {
        {"activating into a directory resets an active glob", "sub", S_IFDIR | 0755, GLOB_NONE, ""},
        {"opening a file leaves an active glob untouched", "main.c", S_IFREG | 0644, GLOB_PLAIN, "report"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_model_with_entry("/home/user", cases[i].name, cases[i].st_mode, 0);
        in.glob_type = GLOB_PLAIN;
        strcpy(in.glob_pattern, "report");
        Msg msg = { .type = MSG_ACTIVATE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.glob_type != cases[i].expected_type || strcmp(out.glob_pattern, cases[i].expected_pattern) != 0) {
            TEST_ERRORF(cases[i].label, "glob = {%d, '%s'}, want {%d, '%s'}",
                        out.glob_type, out.glob_pattern, cases[i].expected_type, cases[i].expected_pattern);
        }
    }
}

static void test_preview(void)
{
    typedef struct {
        const char *label;
        const char *current_path;
        const char *name;
        mode_t st_mode;
        int entry_count;
        int selected;
        CmdType expected_cmd_type;
        const char *expected_cmd_path;
    } Case;

    Case cases[] = {
        {"preview regular file", "/home/user", "main.c", S_IFREG | 0644, 1, 0,
         CMD_PREVIEW, "/home/user/main.c"},
        {"preview directory is a no-op", "/home/user", "sub", S_IFDIR | 0755, 1, 0,
         CMD_NONE, NULL},
        {"preview with nothing selected is a no-op", "/home/user", "", 0, 0, 0,
         CMD_NONE, NULL},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_model_with_entry(cases[i].current_path, cases[i].name,
                                          cases[i].st_mode, cases[i].selected);
        in.entry_count = cases[i].entry_count;
        Msg msg = { .type = MSG_PREVIEW };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != cases[i].expected_cmd_type) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want %d", cmd.type, cases[i].expected_cmd_type);
            continue;
        }
        if (cases[i].expected_cmd_path && strcmp(cmd.path, cases[i].expected_cmd_path) != 0) {
            TEST_ERRORF(cases[i].label, "cmd.path = %s, want %s", cmd.path, cases[i].expected_cmd_path);
        }
    }
}

static void test_dir_loaded(void)
{
    typedef struct {
        const char *label;
        int prior_selected;
        int loaded_count;
        int expected_selected;
    } Case;

    Case cases[] = {
        {"fresh load selects first row", 0, 3, 0},
        {"selection beyond new count falls back to first row", 5, 2, 0},
        {"loading an empty directory selects nothing", 5, 0, 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(9, cases[i].prior_selected);
        static Entry loaded[MAX_ENTRIES];
        for (int j = 0; j < cases[i].loaded_count; j++) {
            snprintf(loaded[j].name, NAME_MAX_LEN, "entry%d", j);
        }
        Msg msg = { .type = MSG_DIR_LOADED };
        msg.dir_loaded.entries = loaded;
        msg.dir_loaded.entry_count = cases[i].loaded_count;
        strcpy(msg.dir_loaded.path, "/loaded/path");
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.entry_count != cases[i].loaded_count) {
            TEST_ERRORF(cases[i].label, "entry_count = %d, want %d", out.entry_count, cases[i].loaded_count);
        }
        if (out.selected != cases[i].expected_selected) {
            TEST_ERRORF(cases[i].label, "selected = %d, want %d", out.selected, cases[i].expected_selected);
        }
        if (strcmp(out.current_path, "/loaded/path") != 0) {
            TEST_ERRORF(cases[i].label, "current_path = %s, want /loaded/path", out.current_path);
        }
        if (cases[i].loaded_count > 0 && strcmp(out.entries[0].name, "entry0") != 0) {
            TEST_ERRORF(cases[i].label, "entries[0].name = %s, want entry0", out.entries[0].name);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static void test_op_succeeded(void)
{
    Model in = make_nav_model(3, 1);
    strcpy(in.current_path, "/home/user");
    in.show_hidden = 1;
    Msg msg = { .type = MSG_OP_SUCCEEDED };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_LOAD_DIR) {
        TEST_ERRORF("op succeeded", "cmd.type = %d, want CMD_LOAD_DIR", cmd.type);
    }
    if (strcmp(cmd.path, "/home/user") != 0) {
        TEST_ERRORF("op succeeded", "cmd.path = %s, want /home/user", cmd.path);
    }
    if (!cmd.show_hidden) {
        TEST_ERRORF("op succeeded", "cmd.show_hidden = %d, want 1 (carried from model)", cmd.show_hidden);
    }
}

static void test_op_succeeded_rebuilds_glob_when_active(void)
{
    Model in = make_nav_model(3, 1);
    strcpy(in.current_path, "/home/user");
    in.glob_type = GLOB_PLAIN;
    strcpy(in.glob_pattern, "report");
    Msg msg = { .type = MSG_OP_SUCCEEDED };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_BUILD_GLOB) {
        TEST_ERRORF("op succeeded rebuilds glob", "cmd.type = %d, want CMD_BUILD_GLOB", cmd.type);
    }
    if (strcmp(cmd.path, "/home/user") != 0) {
        TEST_ERRORF("op succeeded rebuilds glob", "cmd.path = %s, want /home/user", cmd.path);
    }
    if (cmd.glob_type != GLOB_PLAIN || strcmp(cmd.cmd_text, "report") != 0) {
        TEST_ERRORF("op succeeded rebuilds glob", "cmd = {%d, '%s'}, want {GLOB_PLAIN, 'report'}",
                    cmd.glob_type, cmd.cmd_text);
    }
}

static void test_op_failed(void)
{
    Model in = make_nav_model(3, 1);
    Msg msg = { .type = MSG_OP_FAILED };
    strcpy(msg.error, "Permission denied");
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_ERROR) {
        TEST_ERRORF("op failed", "mode = %d, want MODE_ERROR", out.mode);
    }
    if (strcmp(out.error_msg, "Permission denied") != 0) {
        TEST_ERRORF("op failed", "error_msg = %s, want 'Permission denied'", out.error_msg);
    }
}

static void test_start_edit(void)
{
    typedef struct {
        const char *label;
        const char *entry_name;
        MsgType msg_type;
        AppMode expected_mode;
        int expected_selected;
    } Case;

    Case cases[] = {
        {"rename starts edit on unprotected entry", "file.txt", MSG_RENAME, MODE_RENAME, 1},
        {"rename is a no-op on '.'", ".", MSG_RENAME, MODE_NAV, 1},
        {"rename is a no-op on '..'", "..", MSG_RENAME, MODE_NAV, 1},
        {"new appends a virtual row", "file.txt", MSG_NEW, MODE_CREATE, 3},
        {"run cmd enters prompt unconditionally", "file.txt", MSG_RUN_CMD, MODE_RUN_CMD, 1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        strcpy(in.entries[1].name, cases[i].entry_name);
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != cases[i].expected_mode) {
            TEST_ERRORF(cases[i].label, "mode = %d, want %d", out.mode, cases[i].expected_mode);
        }
        if (out.selected != cases[i].expected_selected) {
            TEST_ERRORF(cases[i].label, "selected = %d, want %d", out.selected, cases[i].expected_selected);
        }
        if (out.mode != MODE_NAV && out.edit_len != 0) {
            TEST_ERRORF(cases[i].label, "edit_len = %zu, want 0", out.edit_len);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static void test_enter_filter_mode(void)
{
    typedef struct {
        const char *label;
        MsgType msg_type;
        FilterType prior_type;
        const char *prior_pattern;
        FilterType expected_type;
        const char *expected_edit_buf;
    } Case;

    Case cases[] = {
        {"f with no prior filter starts empty plain composition",
         MSG_FILTER_PLAIN, FILTER_NONE, "", FILTER_PLAIN, ""},
        {"F with no prior filter starts empty regex composition",
         MSG_FILTER_REGEX, FILTER_NONE, "", FILTER_REGEX, ""},
        {"f on an active regex filter prefills pattern and switches type to plain",
         MSG_FILTER_PLAIN, FILTER_REGEX, "\\.(c|h)$", FILTER_PLAIN, "\\.(c|h)$"},
        {"F on an active plain filter prefills pattern and switches type to regex",
         MSG_FILTER_REGEX, FILTER_PLAIN, "report", FILTER_REGEX, "report"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        in.filter_type = cases[i].prior_type;
        strcpy(in.filter_pattern, cases[i].prior_pattern);
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != MODE_FILTER) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_FILTER", out.mode);
        }
        if (out.filter_type != cases[i].expected_type) {
            TEST_ERRORF(cases[i].label, "filter_type = %d, want %d", out.filter_type, cases[i].expected_type);
        }
        if (strcmp(out.edit_buf, cases[i].expected_edit_buf) != 0) {
            TEST_ERRORF(cases[i].label, "edit_buf = '%s', want '%s'", out.edit_buf, cases[i].expected_edit_buf);
        }
        if (out.edit_len != strlen(cases[i].expected_edit_buf)) {
            TEST_ERRORF(cases[i].label, "edit_len = %zu, want %zu", out.edit_len, strlen(cases[i].expected_edit_buf));
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static void test_enter_glob_mode(void)
{
    typedef struct {
        const char *label;
        MsgType msg_type;
        GlobType prior_type;
        const char *prior_pattern;
        GlobType expected_type;
        const char *expected_edit_buf;
    } Case;

    Case cases[] = {
        {"g with no prior glob starts empty plain composition",
         MSG_GLOB_PLAIN, GLOB_NONE, "", GLOB_PLAIN, ""},
        {"G with no prior glob starts empty regex composition",
         MSG_GLOB_REGEX, GLOB_NONE, "", GLOB_REGEX, ""},
        {"g on an active regex glob prefills pattern, switches type to plain",
         MSG_GLOB_PLAIN, GLOB_REGEX, "\\.(c|h)$", GLOB_PLAIN, "\\.(c|h)$"},
        {"G on an active plain glob prefills pattern, switches type to regex",
         MSG_GLOB_REGEX, GLOB_PLAIN, "report", GLOB_REGEX, "report"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        in.glob_type = cases[i].prior_type;
        strcpy(in.glob_pattern, cases[i].prior_pattern);
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != MODE_GLOB) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_GLOB", out.mode);
        }
        if (out.glob_type != cases[i].expected_type) {
            TEST_ERRORF(cases[i].label, "glob_type = %d, want %d", out.glob_type, cases[i].expected_type);
        }
        if (strcmp(out.edit_buf, cases[i].expected_edit_buf) != 0) {
            TEST_ERRORF(cases[i].label, "edit_buf = '%s', want '%s'", out.edit_buf, cases[i].expected_edit_buf);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static void test_enter_glob_mode_entries(void)
{
    Model fresh = make_nav_model(3, 1);
    Msg msg = { .type = MSG_GLOB_PLAIN };
    Model out;
    Cmd cmd;

    update(&msg, &fresh, &out, &cmd);

    if (out.entry_count != 0) {
        TEST_ERRORF("first-time glob entry clears entries", "entry_count = %d, want 0", out.entry_count);
    }

    Model committed = make_nav_model(3, 1);
    committed.glob_type = GLOB_PLAIN;
    strcpy(committed.glob_pattern, "report");

    update(&msg, &committed, &out, &cmd);

    if (out.entry_count != 3) {
        TEST_ERRORF("re-entry on committed glob keeps entries", "entry_count = %d, want 3 (untouched)", out.entry_count);
    }
}

static void test_glob_filter_mutual_exclusion(void)
{
    Model in1 = make_nav_model(3, 1);
    in1.filter_type = FILTER_PLAIN;
    strcpy(in1.filter_pattern, "report");
    Msg msg1 = { .type = MSG_GLOB_PLAIN };
    Model out1;
    Cmd cmd1;
    update(&msg1, &in1, &out1, &cmd1);

    if (out1.filter_type != FILTER_NONE || out1.filter_pattern[0] != '\0') {
        TEST_ERRORF("entering glob clears active flat filter", "filter = {%d, '%s'}, want {FILTER_NONE, ''}",
                    out1.filter_type, out1.filter_pattern);
    }

    Model in2 = make_nav_model(3, 1);
    in2.glob_type = GLOB_PLAIN;
    strcpy(in2.glob_pattern, "report");
    Msg msg2 = { .type = MSG_FILTER_PLAIN };
    Model out2;
    Cmd cmd2;
    update(&msg2, &in2, &out2, &cmd2);

    if (out2.glob_type != GLOB_NONE || out2.glob_pattern[0] != '\0') {
        TEST_ERRORF("entering flat filter clears active glob", "glob = {%d, '%s'}, want {GLOB_NONE, ''}",
                    out2.glob_type, out2.glob_pattern);
    }
}

static Model make_edit_model(AppMode mode, const char *edit_buf, int selected, int entry_count)
{
    Model m = make_nav_model(entry_count, selected);
    m.mode = mode;
    strcpy(m.edit_buf, edit_buf);
    m.edit_len = strlen(edit_buf);
    return m;
}

static Model make_filter_compose_model(FilterType type, const char *edit_buf, int selected)
{
    Model m = make_edit_model(MODE_FILTER, edit_buf, selected, 0);
    m.filter_type = type;

    strcpy(m.unfiltered_entries[0].name, "report.txt");
    strcpy(m.unfiltered_entries[1].name, "other.txt");
    strcpy(m.unfiltered_entries[2].name, "reporter.log");
    m.unfiltered_count = 3;
    return m;
}

static void test_filter_live_recompute_on_text_input(void)
{
    Model in = make_filter_compose_model(FILTER_PLAIN, "repor", 2);
    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 't' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.edit_buf, "report") != 0) {
        TEST_ERRORF("live recompute on text input", "edit_buf = '%s', want 'report'", out.edit_buf);
    }
    if (out.entry_count != 2 || strcmp(out.entries[0].name, "report.txt") != 0 ||
        strcmp(out.entries[1].name, "reporter.log") != 0) {
        TEST_ERRORF("live recompute on text input", "entries = [%s, %s] (%d), want [report.txt, reporter.log] (2)",
                    out.entries[0].name, out.entries[1].name, out.entry_count);
    }
    if (out.selected != 0) {
        TEST_ERRORF("live recompute on text input", "selected = %d, want 0", out.selected);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("live recompute on text input", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_filter_live_recompute_on_delete(void)
{
    Model in = make_filter_compose_model(FILTER_PLAIN, "reporter", 1);
    Msg msg = { .type = MSG_DELETE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.edit_buf, "reporte") != 0) {
        TEST_ERRORF("live recompute on delete", "edit_buf = '%s', want 'reporte'", out.edit_buf);
    }
    if (out.entry_count != 1 || strcmp(out.entries[0].name, "reporter.log") != 0) {
        TEST_ERRORF("live recompute on delete", "entries = [%s] (%d), want [reporter.log] (1)",
                    out.entries[0].name, out.entry_count);
    }
    if (out.selected != 0) {
        TEST_ERRORF("live recompute on delete", "selected = %d, want 0", out.selected);
    }
}

static void test_filter_live_recompute_no_matches_is_empty(void)
{
    Model in = make_filter_compose_model(FILTER_PLAIN, "nonexistent", 0);
    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 'x' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.entry_count != 0) {
        TEST_ERRORF("no matches is empty", "entry_count = %d, want 0", out.entry_count);
    }
}

static void test_filter_live_recompute_malformed_regex_is_empty(void)
{
    Model in = make_filter_compose_model(FILTER_REGEX, "[unterminated", 0);
    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 'x' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.entry_count != 0) {
        TEST_ERRORF("malformed regex is empty", "entry_count = %d, want 0", out.entry_count);
    }
}

static void glob_fixture(Entry *candidates)
{
    strcpy(candidates[0].name, "report.txt");
    strcpy(candidates[1].name, "other.txt");
    strcpy(candidates[2].name, "sub/reporter.log");
}

static Model make_glob_compose_model(GlobType type, const char *edit_buf, Entry *committed_results, int result_count)
{
    Model m = make_edit_model(MODE_GLOB, edit_buf, 0, 0);
    m.glob_type = type;
    for (int i = 0; i < result_count; i++)
        m.entries[i] = committed_results[i];
    m.entry_count = result_count;
    return m;
}

static void test_glob_typing_only_updates_edit_buf(void)
{
    Entry results[3];
    glob_fixture(results);
    Model in = make_glob_compose_model(GLOB_PLAIN, "repor", results, 3);
    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 't' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.edit_buf, "report") != 0) {
        TEST_ERRORF("glob typing updates edit_buf only", "edit_buf = '%s', want 'report'", out.edit_buf);
    }
    if (out.entry_count != 3 || strcmp(out.entries[0].name, "report.txt") != 0 ||
        strcmp(out.entries[1].name, "other.txt") != 0 || strcmp(out.entries[2].name, "sub/reporter.log") != 0) {
        TEST_ERRORF("glob typing updates edit_buf only", "entries = [%s, %s, %s] (%d), want unchanged committed results (3)",
                    out.entries[0].name, out.entries[1].name, out.entries[2].name, out.entry_count);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("glob typing updates edit_buf only", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_glob_deleting_only_updates_edit_buf(void)
{
    Entry results[3];
    glob_fixture(results);
    Model in = make_glob_compose_model(GLOB_PLAIN, "reporter", results, 3);
    Msg msg = { .type = MSG_DELETE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.edit_buf, "reporte") != 0) {
        TEST_ERRORF("glob deleting updates edit_buf only", "edit_buf = '%s', want 'reporte'", out.edit_buf);
    }
    if (out.entry_count != 3 || strcmp(out.entries[0].name, "report.txt") != 0 ||
        strcmp(out.entries[1].name, "other.txt") != 0 || strcmp(out.entries[2].name, "sub/reporter.log") != 0) {
        TEST_ERRORF("glob deleting updates edit_buf only", "entries = [%s, %s, %s] (%d), want unchanged committed results (3)",
                    out.entries[0].name, out.entries[1].name, out.entries[2].name, out.entry_count);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("glob deleting updates edit_buf only", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_glob_commit_on_activate(void)
{
    Entry results[3];
    glob_fixture(results);
    Model in = make_glob_compose_model(GLOB_REGEX, "\\.log$", results, 3);
    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_NAV) {
        TEST_ERRORF("glob commit", "mode = %d, want MODE_NAV", out.mode);
    }
    if (out.glob_type != GLOB_REGEX || strcmp(out.glob_pattern, "\\.log$") != 0) {
        TEST_ERRORF("glob commit", "glob = {%d, '%s'}, want {GLOB_REGEX, '\\.log$'}", out.glob_type, out.glob_pattern);
    }
    if (cmd.type != CMD_BUILD_GLOB) {
        TEST_ERRORF("glob commit", "cmd.type = %d, want CMD_BUILD_GLOB", cmd.type);
    }
    if (cmd.glob_type != GLOB_REGEX || strcmp(cmd.cmd_text, "\\.log$") != 0) {
        TEST_ERRORF("glob commit", "cmd = {%d, '%s'}, want {GLOB_REGEX, '\\.log$'}", cmd.glob_type, cmd.cmd_text);
    }
    if (strcmp(cmd.path, in.current_path) != 0) {
        TEST_ERRORF("glob commit", "cmd.path = '%s', want '%s'", cmd.path, in.current_path);
    }
    if (out.entry_count != 3) {
        TEST_ERRORF("glob commit leaves entries as-is until the walk returns",
                    "entry_count = %d, want 3 (unchanged)", out.entry_count);
    }
}

static void test_glob_cancel_fully_clears(void)
{
    Model in = make_edit_model(MODE_GLOB, "repor", 0, 0);
    in.glob_type = GLOB_PLAIN;
    strcpy(in.glob_pattern, "\\.(c|h)$");
    strcpy(in.unfiltered_entries[0].name, "other.txt");
    strcpy(in.unfiltered_entries[1].name, "report.txt");
    strcpy(in.unfiltered_entries[2].name, "reporter.log");
    in.unfiltered_count = 3;

    Msg msg = { .type = MSG_CANCEL };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_NAV) {
        TEST_ERRORF("glob cancel clears", "mode = %d, want MODE_NAV", out.mode);
    }
    if (out.glob_type != GLOB_NONE) {
        TEST_ERRORF("glob cancel clears", "glob_type = %d, want GLOB_NONE", out.glob_type);
    }
    if (out.glob_pattern[0] != '\0') {
        TEST_ERRORF("glob cancel clears", "glob_pattern = '%s', want empty", out.glob_pattern);
    }
    if (out.entry_count != 3 || strcmp(out.entries[0].name, "other.txt") != 0 ||
        strcmp(out.entries[1].name, "report.txt") != 0 || strcmp(out.entries[2].name, "reporter.log") != 0) {
        TEST_ERRORF("glob cancel clears", "entries = [%s, %s, %s] (%d), want full sorted listing (3)",
                    out.entries[0].name, out.entries[1].name, out.entries[2].name, out.entry_count);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("glob cancel clears", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_glob_built_populates_and_sorts(void)
{
    Entry loaded[3];
    strcpy(loaded[0].name, "other.txt");
    strcpy(loaded[1].name, "report.txt");
    strcpy(loaded[2].name, "sub/reporter.log");

    Model in = make_nav_model(0, 0);
    in.glob_type = GLOB_PLAIN;
    strcpy(in.glob_pattern, "report");
    in.sort_mode = SORT_NAME_DESC;

    Msg msg = { .type = MSG_GLOB_BUILT };
    msg.glob_built.entries = loaded;
    msg.glob_built.entry_count = 3;
    msg.glob_built.truncated = 0;
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.entry_count != 3 || strcmp(out.entries[0].name, "sub/reporter.log") != 0 ||
        strcmp(out.entries[1].name, "report.txt") != 0 || strcmp(out.entries[2].name, "other.txt") != 0) {
        TEST_ERRORF("glob built populates and sorts",
                    "entries = [%s, %s, %s] (%d), want name-desc [sub/reporter.log, report.txt, other.txt] (3)",
                    out.entries[0].name, out.entries[1].name, out.entries[2].name, out.entry_count);
    }
    if (out.selected != 0) {
        TEST_ERRORF("glob built populates and sorts", "selected = %d, want 0", out.selected);
    }
    if (out.glob_capped) {
        TEST_ERRORF("glob built populates and sorts", "glob_capped = 1, want 0");
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("glob built populates and sorts", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_glob_built_sets_capped_flag_without_error(void)
{
    static Entry loaded[MAX_ENTRIES];
    for (int i = 0; i < MAX_ENTRIES; i++)
        snprintf(loaded[i].name, sizeof(loaded[i].name), "match%d.txt", i);

    Model in = make_nav_model(0, 0);
    in.glob_type = GLOB_PLAIN;
    strcpy(in.glob_pattern, ".txt");

    Msg msg = { .type = MSG_GLOB_BUILT };
    msg.glob_built.entries = loaded;
    msg.glob_built.entry_count = MAX_ENTRIES;
    msg.glob_built.truncated = 1;
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_NAV) {
        TEST_ERRORF("glob built capped", "mode = %d, want MODE_NAV (no error transition)", out.mode);
    }
    if (out.entry_count != MAX_ENTRIES) {
        TEST_ERRORF("glob built capped", "entry_count = %d, want %d", out.entry_count, MAX_ENTRIES);
    }
    if (!out.glob_capped) {
        TEST_ERRORF("glob built capped", "glob_capped = 0, want 1");
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("glob built capped", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_error_dismiss_returns_to_nav_even_with_glob_active(void)
{
    Model in = make_nav_model(3, 1);
    in.mode = MODE_ERROR;
    in.glob_type = GLOB_PLAIN;
    strcpy(in.glob_pattern, "report");
    strcpy(in.error_msg, "rename: Permission denied");
    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 'x' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_NAV) {
        TEST_ERRORF("error dismiss ignores unrelated active glob", "mode = %d, want MODE_NAV", out.mode);
    }
}

static void test_edit_text_entry(void)
{
    Model in = make_edit_model(MODE_RENAME, "ab", 1, 3);
    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 'c' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.edit_buf, "abc") != 0 || out.edit_len != 3) {
        TEST_ERRORF("append char", "edit_buf = '%s' (%zu), want 'abc' (3)", out.edit_buf, out.edit_len);
    }
}

static void test_edit_text_entry_full_buffer_is_noop(void)
{
    Model in = make_nav_model(1, 0);
    in.mode = MODE_RENAME;
    memset(in.edit_buf, 'a', NAME_MAX_LEN);
    in.edit_buf[NAME_MAX_LEN] = '\0';
    in.edit_len = NAME_MAX_LEN;
    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 'z' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.edit_len != NAME_MAX_LEN) {
        TEST_ERRORF("full buffer", "edit_len = %zu, want %d (unchanged)", out.edit_len, NAME_MAX_LEN);
    }
}

static void test_edit_backspace(void)
{
    Model in = make_edit_model(MODE_CREATE, "abc", 3, 3);
    Msg msg = { .type = MSG_DELETE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.edit_buf, "ab") != 0 || out.edit_len != 2) {
        TEST_ERRORF("backspace", "edit_buf = '%s' (%zu), want 'ab' (2)", out.edit_buf, out.edit_len);
    }
}

static void test_edit_backspace_on_empty_is_noop(void)
{
    Model in = make_edit_model(MODE_CREATE, "", 3, 3);
    Msg msg = { .type = MSG_DELETE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.edit_len != 0) {
        TEST_ERRORF("backspace on empty", "edit_len = %zu, want 0", out.edit_len);
    }
}

static void test_edit_cancel(void)
{
    typedef struct {
        const char *label;
        AppMode mode;
        int selected;
        int entry_count;
        int expected_selected;
    } Case;

    Case cases[] = {
        {"cancel rename keeps selection", MODE_RENAME, 1, 3, 1},
        {"cancel create clamps virtual-row selection back", MODE_CREATE, 3, 3, 2},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_edit_model(cases[i].mode, "typed", cases[i].selected, cases[i].entry_count);
        Msg msg = { .type = MSG_CANCEL };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != MODE_NAV) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_NAV", out.mode);
        }
        if (out.selected != cases[i].expected_selected) {
            TEST_ERRORF(cases[i].label, "selected = %d, want %d", out.selected, cases[i].expected_selected);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static void test_filter_commit_on_activate(void)
{
    typedef struct {
        const char *label;
        FilterType type;
        const char *edit_buf;
    } Case;

    Case cases[] = {
        {"commit a non-empty plain pattern", FILTER_PLAIN, "report"},
        {"commit a non-empty regex pattern", FILTER_REGEX, "\\.(c|h)$"},
        {"commit an emptied-out pattern clears the filter to show everything", FILTER_PLAIN, ""},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_filter_compose_model(cases[i].type, cases[i].edit_buf, 0);
        Msg msg = { .type = MSG_ACTIVATE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != MODE_NAV) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_NAV", out.mode);
        }
        if (out.filter_type != cases[i].type) {
            TEST_ERRORF(cases[i].label, "filter_type = %d, want %d", out.filter_type, cases[i].type);
        }
        if (strcmp(out.filter_pattern, cases[i].edit_buf) != 0) {
            TEST_ERRORF(cases[i].label, "filter_pattern = '%s', want '%s'", out.filter_pattern, cases[i].edit_buf);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static void test_filter_commit_keeps_last_live_entries(void)
{
    Model in = make_filter_compose_model(FILTER_PLAIN, "report", 0);
    int truncated;
    apply_filter(in.unfiltered_entries, in.unfiltered_count, FILTER_PLAIN, "report", 1,
                 in.sort_mode, in.group_mode, in.entries, MAX_ENTRIES, &in.entry_count, &truncated);

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.entry_count != 2 || strcmp(out.entries[0].name, "report.txt") != 0 ||
        strcmp(out.entries[1].name, "reporter.log") != 0) {
        TEST_ERRORF("commit keeps last live entries", "entries = [%s, %s] (%d), want [report.txt, reporter.log] (2)",
                    out.entries[0].name, out.entries[1].name, out.entry_count);
    }
}

static void test_filter_cancel_fully_clears(void)
{
    Model in = make_filter_compose_model(FILTER_PLAIN, "repor", 0);
    strcpy(in.filter_pattern, "\\.(c|h)$");
    int truncated;
    apply_filter(in.unfiltered_entries, in.unfiltered_count, FILTER_PLAIN, "repor", 1,
                 in.sort_mode, in.group_mode, in.entries, MAX_ENTRIES, &in.entry_count, &truncated);

    Msg msg = { .type = MSG_CANCEL };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_NAV) {
        TEST_ERRORF("filter cancel clears", "mode = %d, want MODE_NAV", out.mode);
    }
    if (out.filter_type != FILTER_NONE) {
        TEST_ERRORF("filter cancel clears", "filter_type = %d, want FILTER_NONE", out.filter_type);
    }
    if (out.filter_pattern[0] != '\0') {
        TEST_ERRORF("filter cancel clears", "filter_pattern = '%s', want empty", out.filter_pattern);
    }
    if (out.entry_count != 3 || strcmp(out.entries[0].name, "other.txt") != 0 ||
        strcmp(out.entries[1].name, "report.txt") != 0 || strcmp(out.entries[2].name, "reporter.log") != 0) {
        TEST_ERRORF("filter cancel clears", "entries = [%s, %s, %s] (%d), want full sorted listing (3)",
                    out.entries[0].name, out.entries[1].name, out.entries[2].name, out.entry_count);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("filter cancel clears", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_validate_empty_cancels(void)
{
    Model in = make_edit_model(MODE_CREATE, "", 3, 3);
    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_NAV) {
        TEST_ERRORF("validate empty", "mode = %d, want MODE_NAV", out.mode);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("validate empty", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_validate_create(void)
{
    typedef struct {
        const char *label;
        const char *edit_buf;
        CmdType expected_cmd_type;
        const char *expected_cmd_path;
    } Case;

    Case cases[] = {
        {"plain name creates a file", "new.txt", CMD_CREATE_FILE, "/tmp/new.txt"},
        {"one trailing slash creates a dir", "newdir/", CMD_CREATE_DIR, "/tmp/newdir"},
        {"multiple trailing slashes creates a dir", "newdir///", CMD_CREATE_DIR, "/tmp/newdir"},
        {"slash-only name cancels silently", "///", CMD_NONE, NULL},
        {"embedded non-trailing slash creates a file, path unchanged", "sub/dir", CMD_CREATE_FILE, "/tmp/sub/dir"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_edit_model(MODE_CREATE, cases[i].edit_buf, 2, 2);
        strcpy(in.current_path, "/tmp");
        Msg msg = { .type = MSG_ACTIVATE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != cases[i].expected_cmd_type) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want %d", cmd.type, cases[i].expected_cmd_type);
            continue;
        }
        if (cases[i].expected_cmd_path && strcmp(cmd.path, cases[i].expected_cmd_path) != 0) {
            TEST_ERRORF(cases[i].label, "cmd.path = %s, want %s", cmd.path, cases[i].expected_cmd_path);
        }
        if (out.mode != MODE_NAV) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_NAV", out.mode);
        }
    }
}

static void test_validate_run_cmd(void)
{
    typedef struct {
        const char *label;
        const char *edit_buf;
        CmdType expected_cmd_type;
        const char *expected_cmd_text;
    } Case;

    Case cases[] = {
        {"empty buffer cancels", "", CMD_NONE, NULL},
        {"missing ! prefix cancels", "ls", CMD_NONE, NULL},
        {"bare ! cancels", "!", CMD_NONE, NULL},
        {"valid command runs", "!ls -la", CMD_RUN, "ls -la"},
        {"prefix plus whitespace-only remainder still runs", "! ", CMD_RUN, " "},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_edit_model(MODE_RUN_CMD, cases[i].edit_buf, 1, 3);
        strcpy(in.current_path, "/tmp");
        Msg msg = { .type = MSG_ACTIVATE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != cases[i].expected_cmd_type) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want %d", cmd.type, cases[i].expected_cmd_type);
            continue;
        }
        if (cases[i].expected_cmd_type == CMD_RUN) {
            if (strcmp(cmd.cmd_text, cases[i].expected_cmd_text) != 0) {
                TEST_ERRORF(cases[i].label, "cmd.cmd_text = '%s', want '%s'", cmd.cmd_text, cases[i].expected_cmd_text);
            }
            if (strcmp(cmd.path, "/tmp") != 0) {
                TEST_ERRORF(cases[i].label, "cmd.path = '%s', want '/tmp' (cwd)", cmd.path);
            }
        }
        if (out.mode != MODE_NAV) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_NAV", out.mode);
        }
    }
}

static void test_validate_run_cmd_carries_selected_path(void)
{
    typedef struct {
        const char *label;
        const char *entry_name;
        int entry_count;
        int selected;
        const char *expected_selected_path;
    } Case;

    Case cases[] = {
        {"selected entry becomes $FILE candidate", "photo.zip", 3, 1, "/tmp/photo.zip"},
        {"empty directory carries no selected path", "", 0, 0, ""},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_edit_model(MODE_RUN_CMD, "!unzip $FILE", cases[i].selected, cases[i].entry_count);
        strcpy(in.current_path, "/tmp");
        if (cases[i].entry_count > 0)
            strcpy(in.entries[cases[i].selected].name, cases[i].entry_name);
        Msg msg = { .type = MSG_ACTIVATE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != CMD_RUN) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_RUN", cmd.type);
            continue;
        }
        if (strcmp(cmd.selected_path, cases[i].expected_selected_path) != 0) {
            TEST_ERRORF(cases[i].label, "cmd.selected_path = '%s', want '%s'",
                        cmd.selected_path, cases[i].expected_selected_path);
        }
    }
}

static void test_validate_rename(void)
{
    typedef struct {
        const char *label;
        const char *entry_name;
        const char *edit_buf;
        const char *expected_path;
        const char *expected_path2;
    } Case;

    Case cases[] = {
        {"flat entry renames in current_path", "old.txt", "new.txt",
         "/tmp/old.txt", "/tmp/new.txt"},
        {"nested glob-match entry renames in its own directory, not current_path",
         "src/old.c", "new.c", "/tmp/src/old.c", "/tmp/src/new.c"},
        {"multi-level nested entry renames in its own directory", "a/b/old.c", "new.c",
         "/tmp/a/b/old.c", "/tmp/a/b/new.c"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_edit_model(MODE_RENAME, cases[i].edit_buf, 1, 3);
        strcpy(in.current_path, "/tmp");
        strcpy(in.entries[1].name, cases[i].entry_name);
        Msg msg = { .type = MSG_ACTIVATE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != CMD_RENAME || strcmp(cmd.path, cases[i].expected_path) != 0 ||
            strcmp(cmd.path2, cases[i].expected_path2) != 0) {
            TEST_ERRORF(cases[i].label, "cmd = {%d, %s, %s}, want {CMD_RENAME, %s, %s}",
                        cmd.type, cmd.path, cmd.path2, cases[i].expected_path, cases[i].expected_path2);
        }
        if (out.mode != MODE_NAV || out.selected != 1) {
            TEST_ERRORF(cases[i].label, "mode/selected = %d/%d, want MODE_NAV/1", out.mode, out.selected);
        }
    }
}

static void test_rename_clears_yank_when_source_matches(void)
{
    Model in = make_edit_model(MODE_RENAME, "new.txt", 1, 3);
    strcpy(in.current_path, "/tmp");
    strcpy(in.entries[1].name, "old.txt");
    strcpy(in.yank_path, "/tmp/old.txt");
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.yank_path[0] != '\0') {
        TEST_ERRORF("rename clears matching yank", "yank_path = '%s', want cleared", out.yank_path);
    }
}

static void test_rename_leaves_yank_for_different_entry(void)
{
    Model in = make_edit_model(MODE_RENAME, "new.txt", 1, 3);
    strcpy(in.current_path, "/tmp");
    strcpy(in.entries[1].name, "old.txt");
    strcpy(in.yank_path, "/tmp/other.txt");
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.yank_path, "/tmp/other.txt") != 0) {
        TEST_ERRORF("rename leaves unrelated yank", "yank_path = '%s', want unchanged '/tmp/other.txt'", out.yank_path);
    }
}

static void test_delete_requests_confirmation(void)
{
    typedef struct {
        const char *label;
        MsgType msg_type;
        const char *entry_name;
        int entry_count;
        int selected;
        AppMode expected_mode;
        int expected_permanent;
    } Case;

    Case cases[] = {
        {"delete on regular entry asks to confirm", MSG_DELETE, "file.txt", 3, 1, MODE_CONFIRM_DELETE, 0},
        {"delete on '.' is a no-op", MSG_DELETE, ".", 3, 1, MODE_NAV, 0},
        {"delete on '..' is a no-op", MSG_DELETE, "..", 3, 1, MODE_NAV, 0},
        {"delete on empty directory is a no-op", MSG_DELETE, "", 0, 0, MODE_NAV, 0},
        {"permanent delete on regular entry asks to confirm", MSG_DELETE_PERMANENT, "file.txt", 3, 1, MODE_CONFIRM_DELETE, 1},
        {"permanent delete on '.' is a no-op", MSG_DELETE_PERMANENT, ".", 3, 1, MODE_NAV, 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(cases[i].entry_count, cases[i].selected);
        strcpy(in.entries[1].name, cases[i].entry_name);
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != cases[i].expected_mode) {
            TEST_ERRORF(cases[i].label, "mode = %d, want %d", out.mode, cases[i].expected_mode);
        }
        if (out.selected != cases[i].selected) {
            TEST_ERRORF(cases[i].label, "selected = %d, want unchanged %d", out.selected, cases[i].selected);
        }
        if (out.confirm_permanent_delete != cases[i].expected_permanent) {
            TEST_ERRORF(cases[i].label, "confirm_permanent_delete = %d, want %d",
                        out.confirm_permanent_delete, cases[i].expected_permanent);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static Model make_confirm_delete_model(const char *name, mode_t st_mode, int selected, int entry_count,
                                        int confirm_permanent)
{
    Model m = make_nav_model(entry_count, selected);
    m.mode = MODE_CONFIRM_DELETE;
    m.confirm_permanent_delete = confirm_permanent;
    strcpy(m.current_path, "/tmp");
    strcpy(m.entries[selected].name, name);
    m.entries[selected].st.st_mode = st_mode;
    return m;
}

static void test_confirm_delete_yes_deletes(void)
{
    typedef struct {
        const char *label;
        char answer;
        mode_t st_mode;
        int confirm_permanent;
        CmdType expected_cmd_type;
        int expected_is_dir;
    } Case;

    Case cases[] = {
        {"lowercase y trashes a file", 'y', S_IFREG | 0644, 0, CMD_TRASH, 0},
        {"uppercase Y trashes a file", 'Y', S_IFREG | 0644, 0, CMD_TRASH, 0},
        {"y trashes a directory", 'y', S_IFDIR | 0755, 0, CMD_TRASH, 1},
        {"y permanently deletes a file", 'y', S_IFREG | 0644, 1, CMD_DELETE, 0},
        {"y permanently deletes a directory", 'y', S_IFDIR | 0755, 1, CMD_DELETE, 1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_confirm_delete_model("target", cases[i].st_mode, 1, 3, cases[i].confirm_permanent);
        Msg msg = { .type = MSG_TEXT_INPUT, .ch = cases[i].answer };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != cases[i].expected_cmd_type || strcmp(cmd.path, "/tmp/target") != 0 ||
            cmd.is_dir != cases[i].expected_is_dir) {
            TEST_ERRORF(cases[i].label, "cmd = {%d, %s, is_dir=%d}, want {%d, /tmp/target, is_dir=%d}",
                        cmd.type, cmd.path, cmd.is_dir, cases[i].expected_cmd_type, cases[i].expected_is_dir);
        }
        if (out.mode != MODE_NAV) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_NAV", out.mode);
        }
    }
}

static void test_confirm_delete_clears_yank_when_target_matches(void)
{
    Model in = make_confirm_delete_model("target", S_IFREG | 0644, 1, 3, 0);
    strcpy(in.yank_path, "/tmp/target");
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 'y' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.yank_path[0] != '\0') {
        TEST_ERRORF("confirm delete clears matching yank", "yank_path = '%s', want cleared", out.yank_path);
    }
}

static void test_confirm_delete_leaves_yank_for_different_entry(void)
{
    Model in = make_confirm_delete_model("target", S_IFREG | 0644, 1, 3, 0);
    strcpy(in.yank_path, "/tmp/other.txt");
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 'y' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.yank_path, "/tmp/other.txt") != 0) {
        TEST_ERRORF("confirm delete leaves unrelated yank", "yank_path = '%s', want unchanged '/tmp/other.txt'",
                    out.yank_path);
    }
}

static void test_confirm_delete_anything_else_cancels(void)
{
    typedef struct {
        const char *label;
        Msg msg;
    } Case;

    Case cases[] = {
        {"lowercase n cancels", { .type = MSG_TEXT_INPUT, .ch = 'n' }},
        {"unrelated char cancels", { .type = MSG_TEXT_INPUT, .ch = 'x' }},
        {"escape cancels", { .type = MSG_CANCEL }},
        {"arrow key cancels", { .type = MSG_MOVE_UP }},
        {"rename key cancels", { .type = MSG_RENAME }},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_confirm_delete_model("target", S_IFREG | 0644, 1, 3, 0);
        Model out;
        Cmd cmd;

        update(&cases[i].msg, &in, &out, &cmd);

        if (out.mode != MODE_NAV) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_NAV", out.mode);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static void test_yank(void)
{
    typedef struct {
        const char *label;
        const char *entry_name;
        MsgType msg_type;
        const char *expected_yank_path;
        int expected_yank_is_move;
    } Case;

    Case cases[] = {
        {"yank copy on unprotected entry", "file.txt", MSG_YANK_COPY, "/tmp/file.txt", 0},
        {"yank move on unprotected entry", "file.txt", MSG_YANK_MOVE, "/tmp/file.txt", 1},
        {"yank copy is a no-op on '.'", ".", MSG_YANK_COPY, "", 0},
        {"yank move is a no-op on '..'", "..", MSG_YANK_MOVE, "", 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        strcpy(in.entries[1].name, cases[i].entry_name);
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (strcmp(out.yank_path, cases[i].expected_yank_path) != 0) {
            TEST_ERRORF(cases[i].label, "yank_path = %s, want %s",
                        out.yank_path, cases[i].expected_yank_path);
        }
        if (out.yank_is_move != cases[i].expected_yank_is_move) {
            TEST_ERRORF(cases[i].label, "yank_is_move = %d, want %d",
                        out.yank_is_move, cases[i].expected_yank_is_move);
        }
        if (out.mode != MODE_NAV) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_NAV (yank must not enter a mode)", out.mode);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static void test_yank_replaces_pending(void)
{
    Model in = make_nav_model(3, 1);
    strcpy(in.entries[1].name, "second.txt");
    strcpy(in.yank_path, "/tmp/first.txt");
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_YANK_COPY };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.yank_path, "/tmp/second.txt") != 0 || out.yank_is_move != 0) {
        TEST_ERRORF("re-yank replaces pending", "yank = {%s, move=%d}, want {/tmp/second.txt, move=0}",
                    out.yank_path, out.yank_is_move);
    }
}

static void test_nav_cancel_clears_pending_yank(void)
{
    Model in = make_nav_model(3, 1);
    strcpy(in.yank_path, "/tmp/first.txt");
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_CANCEL };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.yank_path[0] != '\0') {
        TEST_ERRORF("nav cancel clears yank", "yank_path = '%s', want cleared", out.yank_path);
    }
    if (out.mode != MODE_NAV) {
        TEST_ERRORF("nav cancel clears yank", "mode = %d, want MODE_NAV", out.mode);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("nav cancel clears yank", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_nav_cancel_with_no_pending_yank_is_noop(void)
{
    Model in = make_nav_model(3, 1);

    Msg msg = { .type = MSG_CANCEL };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.yank_path[0] != '\0') {
        TEST_ERRORF("nav cancel noop", "yank_path = '%s', want still empty", out.yank_path);
    }
    if (out.selected != in.selected || out.entry_count != in.entry_count) {
        TEST_ERRORF("nav cancel noop", "model changed unexpectedly");
    }
}

static void test_paste_nothing_pending_is_noop(void)
{
    Model in = make_nav_model(1, 0);
    strcpy(in.current_path, "/tmp");
    strcpy(in.entries[0].name, "existing.txt");

    Msg msg = { .type = MSG_PASTE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("paste nothing pending", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_paste_pending(void)
{
    typedef struct {
        const char *label;
        const char *yank_path;
        int yank_is_move;
        const char *existing_unfiltered_entry;
        const char *narrowed_entry;
        CmdType expected_cmd_type;
        const char *expected_path2;
    } Case;

    Case cases[] = {
        {"paste copy with no collision", "/src/file.txt", 0, "other.txt", "other.txt",
         CMD_COPY, "/tmp/file.txt"},
        {"paste move with no collision", "/src/file.txt", 1, "other.txt", "other.txt",
         CMD_MOVE, "/tmp/file.txt"},
        {"paste with name collision gets a numbered duplicate", "/src/file.txt", 0, "file.txt", "file.txt",
         CMD_COPY, "/tmp/file (1).txt"},
        {"paste while filtered/globbed still checks the real directory contents",
         "/src/file.txt", 0, "file.txt", "unrelated.log",
         CMD_COPY, "/tmp/file (1).txt"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(1, 0);
        strcpy(in.current_path, "/tmp");
        strcpy(in.entries[0].name, cases[i].narrowed_entry);
        strcpy(in.unfiltered_entries[0].name, cases[i].existing_unfiltered_entry);
        in.unfiltered_count = 1;
        strcpy(in.yank_path, cases[i].yank_path);
        in.yank_is_move = cases[i].yank_is_move;

        Msg msg = { .type = MSG_PASTE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != cases[i].expected_cmd_type) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want %d", cmd.type, cases[i].expected_cmd_type);
            continue;
        }
        if (strcmp(cmd.path, cases[i].yank_path) != 0) {
            TEST_ERRORF(cases[i].label, "cmd.path = %s, want %s", cmd.path, cases[i].yank_path);
        }
        if (strcmp(cmd.path2, cases[i].expected_path2) != 0) {
            TEST_ERRORF(cases[i].label, "cmd.path2 = %s, want %s", cmd.path2, cases[i].expected_path2);
        }
        if (out.yank_path[0] != '\0') {
            TEST_ERRORF(cases[i].label, "yank_path = %s, want cleared after paste", out.yank_path);
        }
    }
}

static void test_cycle_page_advances_to_next_page(void)
{
    Model in = make_nav_model(50, 5);
    in.scroll_offset = 0;

    Msg msg = { .type = MSG_CYCLE_PAGE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.selected != 21) {
        TEST_ERRORF("cycle page advances", "selected = %d, want 21", out.selected);
    }
    if (out.scroll_offset != 21) {
        TEST_ERRORF("cycle page advances", "scroll_offset = %d, want 21", out.scroll_offset);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("cycle page advances", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_cycle_page_wraps_from_last_page(void)
{
    Model in = make_nav_model(50, 45);
    in.scroll_offset = 42;

    Msg msg = { .type = MSG_CYCLE_PAGE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.selected != 0) {
        TEST_ERRORF("cycle page wraps", "selected = %d, want 0", out.selected);
    }
    if (out.scroll_offset != 0) {
        TEST_ERRORF("cycle page wraps", "scroll_offset = %d, want 0", out.scroll_offset);
    }
}

static void test_cycle_page_noop_when_everything_fits(void)
{
    Model in = make_nav_model(5, 2);
    in.scroll_offset = 0;

    Msg msg = { .type = MSG_CYCLE_PAGE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.selected != 2) {
        TEST_ERRORF("cycle page noop", "selected = %d, want 2 (unchanged)", out.selected);
    }
    if (out.scroll_offset != 0) {
        TEST_ERRORF("cycle page noop", "scroll_offset = %d, want 0 (unchanged)", out.scroll_offset);
    }
}

static void test_cycle_sort_wraps_through_all_eight_states(void)
{
    SortMode expected[] = {
        SORT_NAME_DESC, SORT_DATE_ASC, SORT_DATE_DESC, SORT_SIZE_ASC,
        SORT_SIZE_DESC, SORT_EXT_ASC, SORT_EXT_DESC, SORT_NAME_ASC,
    };

    Model in = make_nav_model(0, 0);
    Msg msg = { .type = MSG_CYCLE_SORT };

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        Model out;
        Cmd cmd;
        update(&msg, &in, &out, &cmd);

        if (out.sort_mode != expected[i]) {
            TEST_ERRORF("cycle sort", "step %zu: sort_mode = %d, want %d", i, out.sort_mode, expected[i]);
        }
        in = out;
    }
}

static void test_cycle_group_wraps_through_all_three_states(void)
{
    GroupMode expected[] = { GROUP_DIRS_LAST, GROUP_MIXED, GROUP_DIRS_FIRST };

    Model in = make_nav_model(0, 0);
    Msg msg = { .type = MSG_CYCLE_GROUP };

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        Model out;
        Cmd cmd;
        update(&msg, &in, &out, &cmd);

        if (out.group_mode != expected[i]) {
            TEST_ERRORF("cycle group", "step %zu: group_mode = %d, want %d", i, out.group_mode, expected[i]);
        }
        in = out;
    }
}

static void test_toggle_hidden(void)
{
    Model in = make_nav_model(3, 1);
    strcpy(in.current_path, "/home/user");
    in.show_hidden = 0;
    Msg msg = { .type = MSG_TOGGLE_HIDDEN };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (!out.show_hidden) {
        TEST_ERRORF("toggle hidden on", "show_hidden = %d, want 1", out.show_hidden);
    }
    if (cmd.type != CMD_LOAD_DIR || strcmp(cmd.path, "/home/user") != 0 || !cmd.show_hidden) {
        TEST_ERRORF("toggle hidden on", "cmd = {%d, %s, show_hidden=%d}, want {CMD_LOAD_DIR, /home/user, show_hidden=1}",
                    cmd.type, cmd.path, cmd.show_hidden);
    }

    Model in2 = out;
    Model out2;
    Cmd cmd2;
    update(&msg, &in2, &out2, &cmd2);

    if (out2.show_hidden) {
        TEST_ERRORF("toggle hidden off", "show_hidden = %d, want 0", out2.show_hidden);
    }
    if (cmd2.type != CMD_LOAD_DIR || cmd2.show_hidden) {
        TEST_ERRORF("toggle hidden off", "cmd = {%d, show_hidden=%d}, want {CMD_LOAD_DIR, show_hidden=0}",
                    cmd2.type, cmd2.show_hidden);
    }
}

static void test_resort_keeps_selection_on_same_file(void)
{
    Model in = make_nav_model(3, 0);
    strcpy(in.entries[0].name, "zeta.txt");
    strcpy(in.entries[1].name, "alpha.txt");
    strcpy(in.entries[2].name, "middle.txt");
    in.selected = 0; /* zeta.txt, currently first (unsorted) */

    Msg msg = { .type = MSG_CYCLE_SORT }; /* name-asc -> name-desc */
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.entries[out.selected].name, "zeta.txt") != 0) {
        TEST_ERRORF("resort keeps selection", "selected entry = %s, want zeta.txt", out.entries[out.selected].name);
    }
}

static void test_dir_loaded_sorts_entries(void)
{
    Model in = make_nav_model(0, 0);
    static Entry loaded[3];
    strcpy(loaded[0].name, "zeta");
    strcpy(loaded[1].name, "alpha");
    strcpy(loaded[2].name, "middle");

    Msg msg = { .type = MSG_DIR_LOADED };
    msg.dir_loaded.entries = loaded;
    msg.dir_loaded.entry_count = 3;
    strcpy(msg.dir_loaded.path, "/loaded");
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.entries[0].name, "alpha") != 0 ||
        strcmp(out.entries[1].name, "middle") != 0 ||
        strcmp(out.entries[2].name, "zeta") != 0) {
        TEST_ERRORF("dir loaded sorts", "order = %s, %s, %s, want alpha, middle, zeta",
                    out.entries[0].name, out.entries[1].name, out.entries[2].name);
    }
}

static void test_dir_loaded_reapplies_committed_filter(void)
{
    Model in = make_nav_model(0, 0);
    in.filter_type = FILTER_PLAIN;
    strcpy(in.filter_pattern, "report");

    static Entry loaded[4];
    strcpy(loaded[0].name, "report.txt");
    strcpy(loaded[1].name, "other.txt");
    strcpy(loaded[2].name, "reporter.log");
    strcpy(loaded[3].name, "notes.md");

    Msg msg = { .type = MSG_DIR_LOADED };
    msg.dir_loaded.entries = loaded;
    msg.dir_loaded.entry_count = 4;
    strcpy(msg.dir_loaded.path, "/loaded");
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.unfiltered_count != 4) {
        TEST_ERRORF("dir loaded reapplies filter", "unfiltered_count = %d, want 4", out.unfiltered_count);
    }
    if (out.entry_count != 2 || strcmp(out.entries[0].name, "report.txt") != 0 ||
        strcmp(out.entries[1].name, "reporter.log") != 0) {
        TEST_ERRORF("dir loaded reapplies filter", "entries = [%s, %s] (%d), want [report.txt, reporter.log] (2)",
                    out.entries[0].name, out.entries[1].name, out.entry_count);
    }
    if (out.filter_type != FILTER_PLAIN || strcmp(out.filter_pattern, "report") != 0) {
        TEST_ERRORF("dir loaded reapplies filter", "filter = {%d, %s}, want {FILTER_PLAIN, report} (unchanged)",
                    out.filter_type, out.filter_pattern);
    }
}

static void test_quit(void)
{
    Model in = make_nav_model(3, 1);
    Msg msg = { .type = MSG_QUIT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (!out.should_quit) {
        TEST_ERRORF("quit", "should_quit = %d, want 1", out.should_quit);
    }
}

static void test_error_dismissed_by_any_key(void)
{
    Msg cases[] = {
        { .type = MSG_TEXT_INPUT, .ch = 'x' },
        { .type = MSG_MOVE_UP },
        { .type = MSG_CANCEL },
        { .type = MSG_ACTIVATE },
        { .type = MSG_QUIT },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        in.mode = MODE_ERROR;
        strcpy(in.error_msg, "boom");
        Model out;
        Cmd cmd;

        update(&cases[i], &in, &out, &cmd);

        if (out.mode != MODE_NAV) {
            TEST_ERRORF("error dismiss", "mode = %d, want MODE_NAV", out.mode);
        }
        if (out.should_quit) {
            TEST_ERRORF("error dismiss", "should_quit = %d, want 0 (dismiss consumes the key)", out.should_quit);
        }
    }
}

static void test_resize_updates_size_and_recomputes_scroll(void)
{
    Model in = make_nav_model(50, 25);
    in.scroll_offset = 0;

    Msg msg = { .type = MSG_RESIZE };
    msg.resize.width = 100;
    msg.resize.height = 40;
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.term_height != 40 || out.term_width != 100) {
        TEST_ERRORF("resize", "term_height/term_width = %d/%d, want 40/100", out.term_height, out.term_width);
    }
    if (out.scroll_offset != 0) {
        TEST_ERRORF("resize", "scroll_offset = %d, want 0", out.scroll_offset);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("resize", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_move_down_crosses_page_boundary(void)
{
    Model in = make_nav_model(50, 20);
    in.scroll_offset = 0;

    Msg msg = { .type = MSG_MOVE_DOWN };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.selected != 21) {
        TEST_ERRORF("move down crosses page", "selected = %d, want 21", out.selected);
    }
    if (out.scroll_offset != 21) {
        TEST_ERRORF("move down crosses page", "scroll_offset = %d, want 21", out.scroll_offset);
    }
}

static void test_move_up_crosses_page_boundary_back(void)
{
    Model in = make_nav_model(50, 21);
    in.scroll_offset = 21;

    Msg msg = { .type = MSG_MOVE_UP };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.selected != 20) {
        TEST_ERRORF("move up crosses page back", "selected = %d, want 20", out.selected);
    }
    if (out.scroll_offset != 0) {
        TEST_ERRORF("move up crosses page back", "scroll_offset = %d, want 0", out.scroll_offset);
    }
}

static void test_new_leaves_scroll_offset_untouched(void)
{
    Model in = make_nav_model(50, 25);
    in.scroll_offset = 21;

    Msg msg = { .type = MSG_NEW };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_CREATE) {
        TEST_ERRORF("new leaves scroll", "mode = %d, want MODE_CREATE", out.mode);
    }
    if (out.scroll_offset != 21) {
        TEST_ERRORF("new leaves scroll", "scroll_offset = %d, want 21 (untouched)", out.scroll_offset);
    }
}

void test_update(void)
{
    test_move_selection();
    test_resize_updates_size_and_recomputes_scroll();
    test_move_down_crosses_page_boundary();
    test_move_up_crosses_page_boundary_back();
    test_new_leaves_scroll_offset_untouched();
    test_go_parent();
    test_go_parent_resets_active_filter();
    test_go_parent_resets_active_glob();
    test_activate_into_directory_resets_filter_but_opening_a_file_does_not();
    test_activate_into_directory_resets_active_glob_but_opening_a_file_does_not();
    test_activate();
    test_preview();
    test_dir_loaded();
    test_op_succeeded();
    test_op_succeeded_rebuilds_glob_when_active();
    test_op_failed();
    test_start_edit();
    test_enter_filter_mode();
    test_enter_glob_mode();
    test_enter_glob_mode_entries();
    test_glob_filter_mutual_exclusion();
    test_filter_live_recompute_on_text_input();
    test_filter_live_recompute_on_delete();
    test_filter_live_recompute_no_matches_is_empty();
    test_filter_live_recompute_malformed_regex_is_empty();
    test_glob_typing_only_updates_edit_buf();
    test_glob_deleting_only_updates_edit_buf();
    test_glob_commit_on_activate();
    test_glob_cancel_fully_clears();
    test_glob_built_populates_and_sorts();
    test_glob_built_sets_capped_flag_without_error();
    test_error_dismiss_returns_to_nav_even_with_glob_active();
    test_edit_text_entry();
    test_edit_text_entry_full_buffer_is_noop();
    test_edit_backspace();
    test_edit_backspace_on_empty_is_noop();
    test_edit_cancel();
    test_filter_commit_on_activate();
    test_filter_commit_keeps_last_live_entries();
    test_filter_cancel_fully_clears();
    test_validate_empty_cancels();
    test_validate_create();
    test_validate_run_cmd();
    test_validate_run_cmd_carries_selected_path();
    test_validate_rename();
    test_rename_clears_yank_when_source_matches();
    test_rename_leaves_yank_for_different_entry();
    test_delete_requests_confirmation();
    test_confirm_delete_yes_deletes();
    test_confirm_delete_clears_yank_when_target_matches();
    test_confirm_delete_leaves_yank_for_different_entry();
    test_confirm_delete_anything_else_cancels();
    test_yank();
    test_yank_replaces_pending();
    test_nav_cancel_clears_pending_yank();
    test_nav_cancel_with_no_pending_yank_is_noop();
    test_paste_nothing_pending_is_noop();
    test_paste_pending();
    test_cycle_page_advances_to_next_page();
    test_cycle_page_wraps_from_last_page();
    test_cycle_page_noop_when_everything_fits();
    test_cycle_sort_wraps_through_all_eight_states();
    test_cycle_group_wraps_through_all_three_states();
    test_toggle_hidden();
    test_resort_keeps_selection_on_same_file();
    test_dir_loaded_sorts_entries();
    test_dir_loaded_reapplies_committed_filter();
    test_quit();
    test_error_dismissed_by_any_key();
}

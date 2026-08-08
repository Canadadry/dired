#include "minitest.h"
#include "../src/model.h"
#include "../src/msg.h"
#include "../src/cmd.h"
#include "../src/update.h"
#include <string.h>

static Model make_nav_model(int entry_count, int selected)
{
    Model m;
    memset(&m, 0, sizeof(m));
    m.mode = MODE_NAV;
    m.entry_count = entry_count;
    m.selected = selected;
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
        {"selection beyond new count clamps to last row", 5, 2, 1},
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
        {"new file appends a virtual row", "file.txt", MSG_NEW_FILE, MODE_CREATE_FILE, 3},
        {"new dir appends a virtual row", "file.txt", MSG_NEW_DIR, MODE_CREATE_DIR, 3},
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
    Model in = make_edit_model(MODE_CREATE_FILE, "abc", 3, 3);
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
    Model in = make_edit_model(MODE_CREATE_FILE, "", 3, 3);
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
        {"cancel create clamps virtual-row selection back", MODE_CREATE_FILE, 3, 3, 2},
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

static void test_validate_empty_cancels(void)
{
    Model in = make_edit_model(MODE_CREATE_FILE, "", 3, 3);
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

static void test_validate_create_file(void)
{
    Model in = make_edit_model(MODE_CREATE_FILE, "new.txt", 2, 2);
    strcpy(in.current_path, "/tmp");
    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_CREATE_FILE || strcmp(cmd.path, "/tmp/new.txt") != 0) {
        TEST_ERRORF("validate create file", "cmd = {%d, %s}, want {CMD_CREATE_FILE, /tmp/new.txt}",
                    cmd.type, cmd.path);
    }
    if (out.mode != MODE_NAV || out.selected != 1) {
        TEST_ERRORF("validate create file", "mode/selected = %d/%d, want MODE_NAV/1", out.mode, out.selected);
    }
}

static void test_validate_create_dir(void)
{
    Model in = make_edit_model(MODE_CREATE_DIR, "newdir", 2, 2);
    strcpy(in.current_path, "/tmp");
    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_CREATE_DIR || strcmp(cmd.path, "/tmp/newdir") != 0) {
        TEST_ERRORF("validate create dir", "cmd = {%d, %s}, want {CMD_CREATE_DIR, /tmp/newdir}",
                    cmd.type, cmd.path);
    }
}

static void test_validate_rename(void)
{
    Model in = make_edit_model(MODE_RENAME, "new.txt", 1, 3);
    strcpy(in.current_path, "/tmp");
    strcpy(in.entries[1].name, "old.txt");
    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_RENAME || strcmp(cmd.path, "/tmp/old.txt") != 0 ||
        strcmp(cmd.path2, "/tmp/new.txt") != 0) {
        TEST_ERRORF("validate rename", "cmd = {%d, %s, %s}, want {CMD_RENAME, /tmp/old.txt, /tmp/new.txt}",
                    cmd.type, cmd.path, cmd.path2);
    }
    if (out.mode != MODE_NAV || out.selected != 1) {
        TEST_ERRORF("validate rename", "mode/selected = %d/%d, want MODE_NAV/1", out.mode, out.selected);
    }
}

static void test_delete_requests_confirmation(void)
{
    typedef struct {
        const char *label;
        const char *entry_name;
        int entry_count;
        int selected;
        AppMode expected_mode;
    } Case;

    Case cases[] = {
        {"delete on regular entry asks to confirm", "file.txt", 3, 1, MODE_CONFIRM_DELETE},
        {"delete on '.' is a no-op", ".", 3, 1, MODE_NAV},
        {"delete on '..' is a no-op", "..", 3, 1, MODE_NAV},
        {"delete on empty directory is a no-op", "", 0, 0, MODE_NAV},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(cases[i].entry_count, cases[i].selected);
        strcpy(in.entries[1].name, cases[i].entry_name);
        Msg msg = { .type = MSG_DELETE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != cases[i].expected_mode) {
            TEST_ERRORF(cases[i].label, "mode = %d, want %d", out.mode, cases[i].expected_mode);
        }
        if (out.selected != cases[i].selected) {
            TEST_ERRORF(cases[i].label, "selected = %d, want unchanged %d", out.selected, cases[i].selected);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static Model make_confirm_delete_model(const char *name, mode_t st_mode, int selected, int entry_count)
{
    Model m = make_nav_model(entry_count, selected);
    m.mode = MODE_CONFIRM_DELETE;
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
        int expected_is_dir;
    } Case;

    Case cases[] = {
        {"lowercase y deletes a file", 'y', S_IFREG | 0644, 0},
        {"uppercase Y deletes a file", 'Y', S_IFREG | 0644, 0},
        {"y deletes a directory", 'y', S_IFDIR | 0755, 1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_confirm_delete_model("target", cases[i].st_mode, 1, 3);
        Msg msg = { .type = MSG_TEXT_INPUT, .ch = cases[i].answer };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != CMD_DELETE || strcmp(cmd.path, "/tmp/target") != 0 ||
            cmd.is_dir != cases[i].expected_is_dir) {
            TEST_ERRORF(cases[i].label, "cmd = {%d, %s, is_dir=%d}, want {CMD_DELETE, /tmp/target, is_dir=%d}",
                        cmd.type, cmd.path, cmd.is_dir, cases[i].expected_is_dir);
        }
        if (out.mode != MODE_NAV) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_NAV", out.mode);
        }
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
        Model in = make_confirm_delete_model("target", S_IFREG | 0644, 1, 3);
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

void test_update(void)
{
    test_move_selection();
    test_go_parent();
    test_activate();
    test_dir_loaded();
    test_op_succeeded();
    test_op_failed();
    test_start_edit();
    test_edit_text_entry();
    test_edit_text_entry_full_buffer_is_noop();
    test_edit_backspace();
    test_edit_backspace_on_empty_is_noop();
    test_edit_cancel();
    test_validate_empty_cancels();
    test_validate_create_file();
    test_validate_create_dir();
    test_validate_rename();
    test_delete_requests_confirmation();
    test_confirm_delete_yes_deletes();
    test_confirm_delete_anything_else_cancels();
    test_quit();
    test_error_dismissed_by_any_key();
}

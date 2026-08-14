#include "minitest.h"
#include "../src/model.h"
#include "../src/msg.h"
#include "../src/cmd.h"
#include "../src/update.h"
#include "../src/helpers.h"
#include "../src/history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void test_activate_on_archive_file_lists_archive_instead_of_launching_editor(void)
{
    typedef struct {
        const char *label;
        const char *name;
        ArchiveFormat expected_format;
    } Case;

    Case cases[] = {
        {"tar archive", "project.tar", ARCHIVE_TAR},
        {"tar.gz archive", "project.tar.gz", ARCHIVE_TAR},
        {"zip archive", "project.zip", ARCHIVE_ZIP},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_model_with_entry("/home/user", cases[i].name, S_IFREG | 0644, 0);
        Msg msg = { .type = MSG_ACTIVATE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != CMD_LIST_ARCHIVE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_LIST_ARCHIVE", cmd.type);
            continue;
        }
        if (cmd.archive_format != cases[i].expected_format) {
            TEST_ERRORF(cases[i].label, "cmd.archive_format = %d, want %d",
                        cmd.archive_format, cases[i].expected_format);
        }
        char expected_path[PATH_MAX_LEN];
        snprintf(expected_path, sizeof(expected_path), "/home/user/%s", cases[i].name);
        if (strcmp(cmd.path, expected_path) != 0) {
            TEST_ERRORF(cases[i].label, "cmd.path = %s, want %s", cmd.path, expected_path);
        }
    }
}

static void test_activate_on_archive_file_resets_active_filter_and_glob(void)
{
    Model in = make_model_with_entry("/home/user", "project.zip", S_IFREG | 0644, 0);
    in.filter_type = FILTER_PLAIN;
    strcpy(in.filter_pattern, "report");
    in.glob_type = GLOB_PLAIN;
    strcpy(in.glob_pattern, "report");
    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.filter_type != FILTER_NONE || out.filter_pattern[0] != '\0') {
        TEST_ERRORF("activate on archive resets filter", "filter = {%d, '%s'}, want {FILTER_NONE, ''}",
                    out.filter_type, out.filter_pattern);
    }
    if (out.glob_type != GLOB_NONE || out.glob_pattern[0] != '\0') {
        TEST_ERRORF("activate on archive resets glob", "glob = {%d, '%s'}, want {GLOB_NONE, ''}",
                    out.glob_type, out.glob_pattern);
    }
}

static void test_activate_on_non_archive_file_still_launches_editor(void)
{
    Model in = make_model_with_entry("/home/user", "notes.txt", S_IFREG | 0644, 0);
    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_LAUNCH_EDITOR) {
        TEST_ERRORF("non-archive file", "cmd.type = %d, want CMD_LAUNCH_EDITOR", cmd.type);
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

static ArchiveMember make_archive_member(const char *path, int is_dir, long size)
{
    ArchiveMember m = {0};
    strncpy(m.path, path, sizeof(m.path) - 1);
    m.is_dir = is_dir;
    m.size = size;
    m.mtime = 0;
    return m;
}

static void test_archive_listed_pushes_first_level_and_populates_root_entries(void)
{
    ArchiveMember members[] = {
        make_archive_member("readme.txt", 0, 6),
        make_archive_member("src", 1, 0),
        make_archive_member("src/main.c", 0, 42),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_nav_model(0, 0);
    strcpy(in.current_path, "/home/user");

    Msg msg = { .type = MSG_ARCHIVE_LISTED };
    msg.archive_listed.members = members;
    msg.archive_listed.member_count = member_count;
    msg.archive_listed.format = ARCHIVE_ZIP;
    strcpy(msg.archive_listed.path, "/home/user/project.zip");

    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.archive_depth != 1) {
        TEST_ERRORF("archive listed pushes level", "archive_depth = %d, want 1", out.archive_depth);
        return;
    }

    ArchiveLevel *level = &out.archive_stack[0];
    if (level->format != ARCHIVE_ZIP) {
        TEST_ERRORF("archive listed pushes level", "level.format = %d, want ARCHIVE_ZIP", level->format);
    }
    if (strcmp(level->display_name, "project.zip") != 0) {
        TEST_ERRORF("archive listed pushes level", "level.display_name = '%s', want 'project.zip'", level->display_name);
    }
    if (strcmp(level->source_path, "/home/user/project.zip") != 0) {
        TEST_ERRORF("archive listed pushes level", "level.source_path = '%s', want '/home/user/project.zip'", level->source_path);
    }
    if (level->subfolder[0] != '\0') {
        TEST_ERRORF("archive listed pushes level", "level.subfolder = '%s', want ''", level->subfolder);
    }
    if (level->source_is_tmp != 0) {
        TEST_ERRORF("archive listed pushes level", "level.source_is_tmp = %d, want 0", level->source_is_tmp);
    }
    if (level->member_count != member_count) {
        TEST_ERRORF("archive listed pushes level", "level.member_count = %d, want %d", level->member_count, member_count);
    }

    if (strcmp(out.current_path, "/home/user/project.zip") != 0) {
        TEST_ERRORF("archive listed pushes level", "current_path = '%s', want '/home/user/project.zip'", out.current_path);
    }

    if (out.entry_count != 2) {
        TEST_ERRORF("archive listed populates root entries", "entry_count = %d, want 2", out.entry_count);
        return;
    }
    if (strcmp(out.entries[0].name, "src") != 0 || !S_ISDIR(out.entries[0].st.st_mode)) {
        TEST_ERRORF("archive listed populates root entries", "entries[0] = '%s' (dir=%d), want 'src' (dir)",
                    out.entries[0].name, S_ISDIR(out.entries[0].st.st_mode));
    }
    if (strcmp(out.entries[1].name, "readme.txt") != 0 || !S_ISREG(out.entries[1].st.st_mode) ||
        out.entries[1].st.st_size != 6) {
        TEST_ERRORF("archive listed populates root entries",
                    "entries[1] = '%s' (reg=%d, size=%ld), want 'readme.txt' (reg, size 6)",
                    out.entries[1].name, S_ISREG(out.entries[1].st.st_mode), (long)out.entries[1].st.st_size);
    }

    if (out.selected != 0) {
        TEST_ERRORF("archive listed populates root entries", "selected = %d, want 0", out.selected);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("archive listed populates root entries", "cmd.type = %d, want CMD_NONE", cmd.type);
    }

    free(out.archive_stack[0].members);
}

static void test_archive_listed_populates_entries_without_fabricated_permission_bits(void)
{
    ArchiveMember members[] = {
        make_archive_member("readme.txt", 0, 6),
        make_archive_member("src", 1, 0),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_nav_model(0, 0);
    strcpy(in.current_path, "/home/user");

    Msg msg = { .type = MSG_ARCHIVE_LISTED };
    msg.archive_listed.members = members;
    msg.archive_listed.member_count = member_count;
    msg.archive_listed.format = ARCHIVE_ZIP;
    strcpy(msg.archive_listed.path, "/home/user/project.zip");

    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    for (int i = 0; i < out.entry_count; i++) {
        mode_t perm_bits = out.entries[i].st.st_mode & ~S_IFMT;
        if (perm_bits != 0) {
            TEST_ERRORF("archive listed no fabricated permissions",
                        "entries[%d] ('%s') permission bits = %o, want 0 (placeholder, not fabricated)",
                        i, out.entries[i].name, perm_bits);
        }
    }

    free(out.archive_stack[0].members);
}

static void test_archive_listed_beyond_128_members_is_not_truncated(void)
{
    static ArchiveMember members[300];
    for (int i = 0; i < 300; i++) {
        char name[32];
        snprintf(name, sizeof(name), "file%d.txt", i);
        members[i] = make_archive_member(name, 0, i);
    }

    Model in = make_nav_model(0, 0);
    strcpy(in.current_path, "/home/user");

    Msg msg = { .type = MSG_ARCHIVE_LISTED };
    msg.archive_listed.members = members;
    msg.archive_listed.member_count = 300;
    msg.archive_listed.format = ARCHIVE_TAR;
    strcpy(msg.archive_listed.path, "/home/user/project.tar");

    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.archive_stack[0].member_count != 300) {
        TEST_ERRORF("archive listed beyond 128 members is not truncated",
                    "level.member_count = %d, want 300", out.archive_stack[0].member_count);
    }
    if (out.entry_count != 300) {
        TEST_ERRORF("archive listed beyond 128 members is not truncated",
                    "entry_count = %d, want 300", out.entry_count);
    }

    free(out.archive_stack[0].members);
}

static void test_archive_listed_at_max_depth_is_a_noop(void)
{
    ArchiveMember members[] = {
        make_archive_member("readme.txt", 0, 6),
    };

    Model in = make_nav_model(0, 0);
    strcpy(in.current_path, "/home/user");
    in.archive_depth = ARCHIVE_MAX_DEPTH;

    Msg msg = { .type = MSG_ARCHIVE_LISTED };
    msg.archive_listed.members = members;
    msg.archive_listed.member_count = 1;
    msg.archive_listed.format = ARCHIVE_TAR;
    strcpy(msg.archive_listed.path, "/home/user/project.tar");

    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.archive_depth != ARCHIVE_MAX_DEPTH) {
        TEST_ERRORF("archive listed at max depth is a no-op", "archive_depth = %d, want %d",
                    out.archive_depth, ARCHIVE_MAX_DEPTH);
    }
    if (strcmp(out.current_path, "/home/user") != 0) {
        TEST_ERRORF("archive listed at max depth is a no-op", "current_path = '%s', want unchanged '/home/user'",
                    out.current_path);
    }
}

static Model make_archive_level_model(ArchiveMember *members, int member_count,
                                       const char *subfolder, const char *display_name,
                                       const char *current_path)
{
    Model m = make_nav_model(0, 0);
    strcpy(m.current_path, current_path);

    ArchiveLevel *level = &m.archive_stack[0];
    memset(level, 0, sizeof(*level));
    level->format = ARCHIVE_ZIP;
    strncpy(level->display_name, display_name, sizeof(level->display_name) - 1);
    strncpy(level->source_path, current_path, sizeof(level->source_path) - 1);
    strncpy(level->subfolder, subfolder, sizeof(level->subfolder) - 1);
    level->member_count = member_count;
    level->members = malloc(sizeof(ArchiveMember) * member_count);
    memcpy(level->members, members, sizeof(ArchiveMember) * member_count);

    m.archive_depth = 1;
    return m;
}

static void test_activate_on_archive_directory_descends_subfolder_in_place(void)
{
    ArchiveMember members[] = {
        make_archive_member("src", 1, 0),
        make_archive_member("src/main.c", 0, 42),
        make_archive_member("readme.txt", 0, 6),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "project.zip",
                                         "/home/user/project.zip");
    strcpy(in.entries[0].name, "src");
    in.entries[0].st.st_mode = S_IFDIR | 0755;
    in.entry_count = 1;
    in.selected = 0;

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("activate descends archive subfolder", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
    if (strcmp(out.archive_stack[0].subfolder, "src") != 0) {
        TEST_ERRORF("activate descends archive subfolder", "subfolder = '%s', want 'src'",
                    out.archive_stack[0].subfolder);
    }
    if (strcmp(out.current_path, "/home/user/project.zip/src") != 0) {
        TEST_ERRORF("activate descends archive subfolder", "current_path = '%s', want '/home/user/project.zip/src'",
                    out.current_path);
    }
    if (out.entry_count != 1 || strcmp(out.entries[0].name, "main.c") != 0) {
        TEST_ERRORF("activate descends archive subfolder", "entries[0] = '%s' (count %d), want 'main.c' (count 1)",
                    out.entries[0].name, out.entry_count);
    }
    if (out.selected != 0) {
        TEST_ERRORF("activate descends archive subfolder", "selected = %d, want 0", out.selected);
    }

    free(out.archive_stack[0].members);
}

static void test_go_parent_inside_archive_pops_one_subfolder_segment(void)
{
    ArchiveMember members[] = {
        make_archive_member("src", 1, 0),
        make_archive_member("src/assets", 1, 0),
        make_archive_member("src/assets/logo.png", 0, 100),
        make_archive_member("src/main.c", 0, 42),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "src/assets", "project.zip",
                                         "/home/user/project.zip/src/assets");

    Msg msg = { .type = MSG_GO_PARENT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("go parent pops one archive subfolder segment", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
    if (out.archive_depth != 1) {
        TEST_ERRORF("go parent pops one archive subfolder segment", "archive_depth = %d, want 1", out.archive_depth);
    }
    if (strcmp(out.archive_stack[0].subfolder, "src") != 0) {
        TEST_ERRORF("go parent pops one archive subfolder segment", "subfolder = '%s', want 'src'",
                    out.archive_stack[0].subfolder);
    }
    if (strcmp(out.current_path, "/home/user/project.zip/src") != 0) {
        TEST_ERRORF("go parent pops one archive subfolder segment", "current_path = '%s', want '/home/user/project.zip/src'",
                    out.current_path);
    }
    if (out.entry_count != 2) {
        TEST_ERRORF("go parent pops one archive subfolder segment", "entry_count = %d, want 2", out.entry_count);
    }

    free(out.archive_stack[0].members);
}

static void test_go_parent_at_archive_root_pops_level_to_real_filesystem(void)
{
    ArchiveMember members[] = {
        make_archive_member("readme.txt", 0, 6),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "project.zip",
                                         "/home/user/project.zip");
    in.show_hidden = 1;

    Msg msg = { .type = MSG_GO_PARENT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.archive_depth != 0) {
        TEST_ERRORF("go parent at archive root pops level to real fs", "archive_depth = %d, want 0", out.archive_depth);
    }
    if (cmd.type != CMD_LOAD_DIR) {
        TEST_ERRORF("go parent at archive root pops level to real fs", "cmd.type = %d, want CMD_LOAD_DIR", cmd.type);
    }
    if (strcmp(cmd.path, "/home/user") != 0) {
        TEST_ERRORF("go parent at archive root pops level to real fs", "cmd.path = '%s', want '/home/user'", cmd.path);
    }
    if (!cmd.show_hidden) {
        TEST_ERRORF("go parent at archive root pops level to real fs", "cmd.show_hidden = %d, want 1", cmd.show_hidden);
    }
}

static void test_go_parent_at_nested_archive_root_pops_to_containing_level(void)
{
    ArchiveMember outer_members[] = {
        make_archive_member("sub1", 1, 0),
        make_archive_member("sub1/inner.zip", 0, 500),
        make_archive_member("sub1/other.txt", 0, 10),
    };
    int outer_count = sizeof(outer_members) / sizeof(outer_members[0]);

    ArchiveMember inner_members[] = {
        make_archive_member("a.txt", 0, 1),
    };
    int inner_count = sizeof(inner_members) / sizeof(inner_members[0]);

    Model in = make_archive_level_model(outer_members, outer_count, "sub1", "outer.tar",
                                         "/home/user/outer.tar/sub1");

    ArchiveLevel *inner = &in.archive_stack[1];
    memset(inner, 0, sizeof(*inner));
    inner->format = ARCHIVE_ZIP;
    strcpy(inner->display_name, "inner.zip");
    strcpy(inner->source_path, "/tmp/inner-extracted.zip");
    inner->subfolder[0] = '\0';
    inner->source_is_tmp = 1;
    inner->member_count = inner_count;
    inner->members = malloc(sizeof(ArchiveMember) * inner_count);
    memcpy(inner->members, inner_members, sizeof(ArchiveMember) * inner_count);
    in.archive_depth = 2;
    strcpy(in.current_path, "/home/user/outer.tar/sub1/inner.zip");

    Msg msg = { .type = MSG_GO_PARENT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("go parent at nested archive root pops to containing level", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
    if (out.archive_depth != 1) {
        TEST_ERRORF("go parent at nested archive root pops to containing level", "archive_depth = %d, want 1",
                    out.archive_depth);
    }
    if (strcmp(out.current_path, "/home/user/outer.tar/sub1") != 0) {
        TEST_ERRORF("go parent at nested archive root pops to containing level", "current_path = '%s', want '/home/user/outer.tar/sub1'",
                    out.current_path);
    }
    if (out.entry_count != 2) {
        TEST_ERRORF("go parent at nested archive root pops to containing level", "entry_count = %d, want 2", out.entry_count);
    } else {
        int found_inner = 0, found_other = 0;
        for (int i = 0; i < out.entry_count; i++) {
            if (strcmp(out.entries[i].name, "inner.zip") == 0)
                found_inner = 1;
            if (strcmp(out.entries[i].name, "other.txt") == 0)
                found_other = 1;
        }
        if (!found_inner || !found_other) {
            TEST_ERRORF("go parent at nested archive root pops to containing level",
                        "entries missing expected names (found_inner=%d, found_other=%d)", found_inner, found_other);
        }
    }

    free(out.archive_stack[0].members);
}

static void test_activate_on_archive_member_inside_archive_extracts_member_first(void)
{
    ArchiveMember members[] = {
        make_archive_member("sub1", 1, 0),
        make_archive_member("sub1/inner.zip", 0, 500),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "sub1", "outer.tar",
                                         "/home/user/outer.tar/sub1");
    in.archive_stack[0].format = ARCHIVE_TAR;
    strcpy(in.archive_stack[0].source_path, "/home/user/outer.tar");
    strcpy(in.entries[0].name, "inner.zip");
    in.entries[0].st.st_mode = S_IFREG | 0644;
    in.entry_count = 1;
    in.selected = 0;

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_EXTRACT_MEMBER) {
        TEST_ERRORF("activate on nested archive member extracts first",
                    "cmd.type = %d, want CMD_EXTRACT_MEMBER", cmd.type);
    }
    if (cmd.archive_format != ARCHIVE_TAR) {
        TEST_ERRORF("activate on nested archive member extracts first",
                    "cmd.archive_format = %d, want ARCHIVE_TAR", cmd.archive_format);
    }
    if (strcmp(cmd.path, "/home/user/outer.tar") != 0) {
        TEST_ERRORF("activate on nested archive member extracts first",
                    "cmd.path = '%s', want '/home/user/outer.tar'", cmd.path);
    }
    if (strcmp(cmd.path2, "sub1/inner.zip") != 0) {
        TEST_ERRORF("activate on nested archive member extracts first",
                    "cmd.path2 = '%s', want 'sub1/inner.zip'", cmd.path2);
    }
    if (out.archive_depth != 1) {
        TEST_ERRORF("activate on nested archive member extracts first",
                    "archive_depth = %d, want unchanged 1", out.archive_depth);
    }
    if (out.entry_count != 1 || strcmp(out.entries[0].name, "inner.zip") != 0) {
        TEST_ERRORF("activate on nested archive member extracts first",
                    "entries unexpectedly changed before extraction completes");
    }

    free(out.archive_stack[0].members);
}

static void test_activate_on_archive_member_at_max_depth_does_not_extract(void)
{
    ArchiveMember members[] = {
        make_archive_member("inner.zip", 0, 500),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "level4.tar",
                                         "/home/user/l1.tar/l2.tar/l3.tar/level4.tar");
    in.archive_depth = ARCHIVE_MAX_DEPTH;
    strcpy(in.entries[0].name, "inner.zip");
    in.entries[0].st.st_mode = S_IFREG | 0644;
    in.entry_count = 1;
    in.selected = 0;

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("activate on archive member at max depth is a no-op",
                    "cmd.type = %d, want CMD_NONE", cmd.type);
    }
    if (out.archive_depth != ARCHIVE_MAX_DEPTH) {
        TEST_ERRORF("activate on archive member at max depth is a no-op",
                    "archive_depth = %d, want %d", out.archive_depth, ARCHIVE_MAX_DEPTH);
    }

    free(out.archive_stack[0].members);
}

static void test_activate_on_plain_file_inside_archive_opens_archive_member(void)
{
    ArchiveMember members[] = {
        make_archive_member("sub1", 1, 0),
        make_archive_member("sub1/notes.txt", 0, 12),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "sub1", "outer.tar",
                                         "/home/user/outer.tar/sub1");
    in.archive_stack[0].format = ARCHIVE_TAR;
    strcpy(in.archive_stack[0].source_path, "/home/user/outer.tar");
    strcpy(in.entries[0].name, "notes.txt");
    in.entries[0].st.st_mode = S_IFREG | 0644;
    in.entry_count = 1;
    in.selected = 0;

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_OPEN_ARCHIVE_MEMBER) {
        TEST_ERRORF("activate on plain file inside archive opens archive member",
                    "cmd.type = %d, want CMD_OPEN_ARCHIVE_MEMBER", cmd.type);
    }
    if (cmd.archive_format != ARCHIVE_TAR) {
        TEST_ERRORF("activate on plain file inside archive opens archive member",
                    "cmd.archive_format = %d, want ARCHIVE_TAR", cmd.archive_format);
    }
    if (strcmp(cmd.path, "/home/user/outer.tar") != 0) {
        TEST_ERRORF("activate on plain file inside archive opens archive member",
                    "cmd.path = '%s', want '/home/user/outer.tar'", cmd.path);
    }
    if (strcmp(cmd.path2, "sub1/notes.txt") != 0) {
        TEST_ERRORF("activate on plain file inside archive opens archive member",
                    "cmd.path2 = '%s', want 'sub1/notes.txt'", cmd.path2);
    }
    if (out.archive_depth != 1) {
        TEST_ERRORF("activate on plain file inside archive opens archive member",
                    "archive_depth = %d, want unchanged 1", out.archive_depth);
    }
    if (out.entry_count != 1 || strcmp(out.entries[0].name, "notes.txt") != 0) {
        TEST_ERRORF("activate on plain file inside archive opens archive member",
                    "entries unexpectedly changed");
    }

    free(out.archive_stack[0].members);
}

static void test_preview_on_file_inside_archive_previews_archive_member(void)
{
    ArchiveMember members[] = {
        make_archive_member("sub1", 1, 0),
        make_archive_member("sub1/notes.txt", 0, 12),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "sub1", "outer.tar",
                                         "/home/user/outer.tar/sub1");
    in.archive_stack[0].format = ARCHIVE_TAR;
    strcpy(in.archive_stack[0].source_path, "/home/user/outer.tar");
    strcpy(in.entries[0].name, "notes.txt");
    in.entries[0].st.st_mode = S_IFREG | 0644;
    in.entry_count = 1;
    in.selected = 0;

    Msg msg = { .type = MSG_PREVIEW };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_PREVIEW_ARCHIVE_MEMBER) {
        TEST_ERRORF("preview on file inside archive previews archive member",
                    "cmd.type = %d, want CMD_PREVIEW_ARCHIVE_MEMBER", cmd.type);
    }
    if (cmd.archive_format != ARCHIVE_TAR) {
        TEST_ERRORF("preview on file inside archive previews archive member",
                    "cmd.archive_format = %d, want ARCHIVE_TAR", cmd.archive_format);
    }
    if (strcmp(cmd.path, "/home/user/outer.tar") != 0) {
        TEST_ERRORF("preview on file inside archive previews archive member",
                    "cmd.path = '%s', want '/home/user/outer.tar'", cmd.path);
    }
    if (strcmp(cmd.path2, "sub1/notes.txt") != 0) {
        TEST_ERRORF("preview on file inside archive previews archive member",
                    "cmd.path2 = '%s', want 'sub1/notes.txt'", cmd.path2);
    }
    if (out.archive_depth != 1) {
        TEST_ERRORF("preview on file inside archive previews archive member",
                    "archive_depth = %d, want unchanged 1", out.archive_depth);
    }

    free(out.archive_stack[0].members);
}

static void test_member_extracted_issues_list_archive_for_nested_format(void)
{
    ArchiveMember dummy[] = { make_archive_member("placeholder", 0, 1) };
    Model in = make_archive_level_model(dummy, 1, "sub1", "outer.tar", "/home/user/outer.tar/sub1");

    Msg msg = { .type = MSG_MEMBER_EXTRACTED };
    strcpy(msg.member_extracted.tmp_path, "/tmp/dired-archive-abc123");
    strcpy(msg.member_extracted.member_path, "sub1/inner.zip");

    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_LIST_ARCHIVE) {
        TEST_ERRORF("member extracted issues list archive", "cmd.type = %d, want CMD_LIST_ARCHIVE", cmd.type);
    }
    if (cmd.archive_format != ARCHIVE_ZIP) {
        TEST_ERRORF("member extracted issues list archive", "cmd.archive_format = %d, want ARCHIVE_ZIP", cmd.archive_format);
    }
    if (strcmp(cmd.path, "/tmp/dired-archive-abc123") != 0) {
        TEST_ERRORF("member extracted issues list archive", "cmd.path = '%s', want tmp path", cmd.path);
    }
    if (strcmp(cmd.path2, "inner.zip") != 0) {
        TEST_ERRORF("member extracted issues list archive", "cmd.path2 = '%s', want 'inner.zip'", cmd.path2);
    }
    if (!cmd.is_dir) {
        TEST_ERRORF("member extracted issues list archive", "cmd.is_dir (source_is_tmp) = %d, want 1", cmd.is_dir);
    }
    if (out.archive_depth != 1) {
        TEST_ERRORF("member extracted issues list archive", "archive_depth = %d, want unchanged 1", out.archive_depth);
    }

    free(out.archive_stack[0].members);
}

static void test_archive_listed_from_tmp_source_pushes_nested_level(void)
{
    ArchiveMember outer_members[] = {
        make_archive_member("sub1", 1, 0),
        make_archive_member("sub1/inner.zip", 0, 500),
    };
    int outer_count = sizeof(outer_members) / sizeof(outer_members[0]);

    Model in = make_archive_level_model(outer_members, outer_count, "sub1", "outer.tar",
                                         "/home/user/outer.tar/sub1");

    ArchiveMember inner_members[] = {
        make_archive_member("a.txt", 0, 1),
    };
    int inner_count = sizeof(inner_members) / sizeof(inner_members[0]);

    Msg msg = { .type = MSG_ARCHIVE_LISTED };
    msg.archive_listed.members = inner_members;
    msg.archive_listed.member_count = inner_count;
    msg.archive_listed.format = ARCHIVE_ZIP;
    strcpy(msg.archive_listed.path, "/tmp/dired-archive-abc123");
    strcpy(msg.archive_listed.display_name, "inner.zip");
    msg.archive_listed.source_is_tmp = 1;

    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.archive_depth != 2) {
        TEST_ERRORF("archive listed from tmp source pushes nested level",
                    "archive_depth = %d, want 2", out.archive_depth);
    }

    ArchiveLevel *level = &out.archive_stack[1];
    if (level->format != ARCHIVE_ZIP) {
        TEST_ERRORF("archive listed from tmp source pushes nested level",
                    "level.format = %d, want ARCHIVE_ZIP", level->format);
    }
    if (strcmp(level->display_name, "inner.zip") != 0) {
        TEST_ERRORF("archive listed from tmp source pushes nested level",
                    "level.display_name = '%s', want 'inner.zip'", level->display_name);
    }
    if (strcmp(level->source_path, "/tmp/dired-archive-abc123") != 0) {
        TEST_ERRORF("archive listed from tmp source pushes nested level",
                    "level.source_path = '%s', want '/tmp/dired-archive-abc123'", level->source_path);
    }
    if (!level->source_is_tmp) {
        TEST_ERRORF("archive listed from tmp source pushes nested level",
                    "level.source_is_tmp = %d, want 1", level->source_is_tmp);
    }
    if (strcmp(out.current_path, "/home/user/outer.tar/sub1/inner.zip") != 0) {
        TEST_ERRORF("archive listed from tmp source pushes nested level",
                    "current_path = '%s', want '/home/user/outer.tar/sub1/inner.zip'", out.current_path);
    }
    if (out.entry_count != 1 || strcmp(out.entries[0].name, "a.txt") != 0) {
        TEST_ERRORF("archive listed from tmp source pushes nested level",
                    "entries[0] = '%s' (count %d), want 'a.txt' (count 1)",
                    out.entries[0].name, out.entry_count);
    }

    free(out.archive_stack[0].members);
    free(out.archive_stack[1].members);
}

static void test_go_parent_at_nested_archive_root_deletes_tmp_source_file(void)
{
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";

    char tmpl[PATH_MAX_LEN];
    snprintf(tmpl, sizeof(tmpl), "%s/dired_update_test_XXXXXX", tmpdir);
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        TEST_ERRORF("setup", "mkstemp failed");
        return;
    }
    close(fd);

    ArchiveMember outer_members[] = {
        make_archive_member("sub1", 1, 0),
        make_archive_member("sub1/inner.zip", 0, 500),
    };
    int outer_count = sizeof(outer_members) / sizeof(outer_members[0]);

    Model in = make_archive_level_model(outer_members, outer_count, "sub1", "outer.tar",
                                         "/home/user/outer.tar/sub1");

    ArchiveMember inner_members[] = {
        make_archive_member("a.txt", 0, 1),
    };
    int inner_count = sizeof(inner_members) / sizeof(inner_members[0]);

    ArchiveLevel *inner = &in.archive_stack[1];
    memset(inner, 0, sizeof(*inner));
    inner->format = ARCHIVE_ZIP;
    strcpy(inner->display_name, "inner.zip");
    strncpy(inner->source_path, tmpl, sizeof(inner->source_path) - 1);
    inner->source_is_tmp = 1;
    inner->member_count = inner_count;
    inner->members = malloc(sizeof(ArchiveMember) * inner_count);
    memcpy(inner->members, inner_members, sizeof(ArchiveMember) * inner_count);
    in.archive_depth = 2;
    strcpy(in.current_path, "/home/user/outer.tar/sub1/inner.zip");

    Msg msg = { .type = MSG_GO_PARENT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.archive_depth != 1) {
        TEST_ERRORF("go parent at nested archive root deletes tmp source file",
                    "archive_depth = %d, want 1", out.archive_depth);
    }
    if (access(tmpl, F_OK) == 0) {
        TEST_ERRORF("go parent at nested archive root deletes tmp source file",
                    "tmp file '%s' still exists after pop", tmpl);
    }

    free(out.archive_stack[0].members);
}

static void test_filter_inside_archive_narrows_from_level_current_subfolder(void)
{
    ArchiveMember members[] = {
        make_archive_member("report.txt", 0, 6),
        make_archive_member("other.txt", 0, 4),
        make_archive_member("reporter.log", 0, 9),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_nav_model(0, 0);
    strcpy(in.current_path, "/home/user");

    Msg listed_msg = { .type = MSG_ARCHIVE_LISTED };
    listed_msg.archive_listed.members = members;
    listed_msg.archive_listed.member_count = member_count;
    listed_msg.archive_listed.format = ARCHIVE_ZIP;
    strcpy(listed_msg.archive_listed.path, "/home/user/project.zip");

    Model after_listed;
    Cmd cmd;
    update(&listed_msg, &in, &after_listed, &cmd);

    if (after_listed.unfiltered_count != 3) {
        TEST_ERRORF("filter inside archive narrows", "unfiltered_count = %d, want 3 (synced from archive level)",
                    after_listed.unfiltered_count);
        free(after_listed.archive_stack[0].members);
        return;
    }

    after_listed.mode = MODE_FILTER;
    after_listed.filter_type = FILTER_PLAIN;
    strcpy(after_listed.edit_buf, "repor");
    after_listed.edit_len = strlen(after_listed.edit_buf);

    Msg text_msg = { .type = MSG_TEXT_INPUT, .ch = 't' };
    Model out;
    update(&text_msg, &after_listed, &out, &cmd);

    if (out.entry_count != 2 || strcmp(out.entries[0].name, "report.txt") != 0 ||
        strcmp(out.entries[1].name, "reporter.log") != 0) {
        TEST_ERRORF("filter inside archive narrows",
                    "entries = [%s, %s] (%d), want [report.txt, reporter.log] (2)",
                    out.entries[0].name, out.entries[1].name, out.entry_count);
    }

    free(out.archive_stack[0].members);
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

static void test_op_succeeded_inside_archive_refreshes_entries_in_place(void)
{
    ArchiveMember members[] = {
        make_archive_member("src", 1, 0),
        make_archive_member("src/main.c", 0, 42),
        make_archive_member("readme.txt", 0, 6),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "project.zip",
                                         "/home/user/project.zip");
    in.entry_count = 2;
    strcpy(in.entries[0].name, "src");
    in.entries[0].st.st_mode = S_IFDIR | 0755;
    strcpy(in.entries[1].name, "readme.txt");
    in.entries[1].st.st_mode = S_IFREG | 0644;
    in.selected = 1;

    Msg msg = { .type = MSG_OP_SUCCEEDED };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("op succeeded inside archive refreshes in place",
                    "cmd.type = %d, want CMD_NONE (no real-filesystem reload of a virtual path)", cmd.type);
    }
    if (out.archive_depth != 1) {
        TEST_ERRORF("op succeeded inside archive refreshes in place",
                    "archive_depth = %d, want unchanged 1", out.archive_depth);
    }
    if (out.entry_count != 2 || strcmp(out.entries[0].name, "src") != 0 ||
        strcmp(out.entries[1].name, "readme.txt") != 0) {
        TEST_ERRORF("op succeeded inside archive refreshes in place",
                    "entries = [%s, %s] (%d), want [src, readme.txt] (2)",
                    out.entries[0].name, out.entries[1].name, out.entry_count);
    }
    if (out.selected != 1) {
        TEST_ERRORF("op succeeded inside archive refreshes in place",
                    "selected = %d, want unchanged 1", out.selected);
    }

    free(out.archive_stack[0].members);
}

static void test_op_succeeded_inside_archive_with_glob_active_recomputes_glob_in_place(void)
{
    ArchiveMember members[] = {
        make_archive_member("readme.txt", 0, 6),
        make_archive_member("src", 1, 0),
        make_archive_member("src/main.c", 0, 42),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "project.zip",
                                         "/home/user/project.zip");
    in.glob_type = GLOB_PLAIN;
    strcpy(in.glob_pattern, ".c");
    in.entry_count = 1;
    strcpy(in.entries[0].name, "src/main.c");
    in.entries[0].st.st_mode = S_IFREG | 0644;
    in.selected = 0;

    Msg msg = { .type = MSG_OP_SUCCEEDED };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("op succeeded inside archive with glob active recomputes in place",
                    "cmd.type = %d, want CMD_NONE (no real-filesystem walk)", cmd.type);
    }
    if (out.entry_count != 1 || strcmp(out.entries[0].name, "src/main.c") != 0) {
        TEST_ERRORF("op succeeded inside archive with glob active recomputes in place",
                    "entries = [%s] (%d), want [src/main.c] (1)",
                    out.entries[0].name, out.entry_count);
    }

    free(out.archive_stack[0].members);
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

static void test_enter_select_mode_from_nav(void)
{
    Model in = make_nav_model(3, 1);
    Msg msg = { .type = MSG_TOGGLE_SELECT_MODE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_SELECT) {
        TEST_ERRORF("v enters select mode", "mode = %d, want MODE_SELECT", out.mode);
    }
    if (out.selected != 1) {
        TEST_ERRORF("v enters select mode", "selected = %d, want 1 (entering does not mark/move cursor)", out.selected);
    }
    if (out.entry_count != 3) {
        TEST_ERRORF("v enters select mode", "entry_count = %d, want 3 (unaffected)", out.entry_count);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("v enters select mode", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static void test_leave_select_mode(void)
{
    typedef struct {
        const char *label;
        MsgType msg_type;
    } Case;

    Case cases[] = {
        {"v toggles back to nav", MSG_TOGGLE_SELECT_MODE},
        {"Esc returns to nav", MSG_CANCEL},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        in.mode = MODE_SELECT;
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != MODE_NAV) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_NAV", out.mode);
        }
    }
}

static void test_select_mode_rejected_while_glob_active(void)
{
    Model in = make_nav_model(3, 1);
    in.glob_type = GLOB_PLAIN;
    strcpy(in.glob_pattern, "report");
    Msg msg = { .type = MSG_TOGGLE_SELECT_MODE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_ERROR) {
        TEST_ERRORF("v blocked on active glob", "mode = %d, want MODE_ERROR", out.mode);
    }
    if (out.error_msg[0] == '\0') {
        TEST_ERRORF("v blocked on active glob", "error_msg = '', want a non-empty message");
    }
    if (out.glob_type != GLOB_PLAIN || strcmp(out.glob_pattern, "report") != 0) {
        TEST_ERRORF("v blocked on active glob", "glob = {%d, '%s'}, want {GLOB_PLAIN, 'report'} (untouched)",
                    out.glob_type, out.glob_pattern);
    }
}

static void test_select_mode_allowed_while_filter_active(void)
{
    Model in = make_nav_model(3, 1);
    in.filter_type = FILTER_PLAIN;
    strcpy(in.filter_pattern, "report");
    Msg msg = { .type = MSG_TOGGLE_SELECT_MODE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_SELECT) {
        TEST_ERRORF("v allowed with active filter", "mode = %d, want MODE_SELECT", out.mode);
    }
    if (out.filter_type != FILTER_PLAIN || strcmp(out.filter_pattern, "report") != 0) {
        TEST_ERRORF("v allowed with active filter", "filter = {%d, '%s'}, want {FILTER_PLAIN, 'report'} (untouched)",
                    out.filter_type, out.filter_pattern);
    }
}

static void test_toggle_mark_marks_unmarked_entry(void)
{
    Model in = make_nav_model(3, 1);
    in.mode = MODE_SELECT;
    Msg msg = { .type = MSG_TOGGLE_MARK };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (!out.marked[1]) {
        TEST_ERRORF("space marks unmarked entry", "marked[1] = %d, want 1", out.marked[1]);
    }
    if (out.marked_count != 1) {
        TEST_ERRORF("space marks unmarked entry", "marked_count = %d, want 1", out.marked_count);
    }
}

static void test_toggle_mark_unmarks_marked_entry(void)
{
    Model in = make_nav_model(3, 1);
    in.mode = MODE_SELECT;
    in.marked[1] = 1;
    in.marked_count = 1;
    strcpy(in.marked_dir, in.current_path);
    Msg msg = { .type = MSG_TOGGLE_MARK };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.marked[1]) {
        TEST_ERRORF("space unmarks marked entry", "marked[1] = %d, want 0", out.marked[1]);
    }
    if (out.marked_count != 0) {
        TEST_ERRORF("space unmarks marked entry", "marked_count = %d, want 0", out.marked_count);
    }
}

static void test_toggle_mark_all_marks_everything_when_not_all_marked(void)
{
    Model in = make_nav_model(3, 1);
    in.mode = MODE_SELECT;
    in.marked[0] = 1;
    in.marked_count = 1;
    strcpy(in.marked_dir, in.current_path);
    Msg msg = { .type = MSG_TOGGLE_MARK_ALL };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    for (int i = 0; i < 3; i++) {
        if (!out.marked[i]) {
            TEST_ERRORF("t marks everything when not all marked", "marked[%d] = %d, want 1", i, out.marked[i]);
        }
    }
    if (out.marked_count != 3) {
        TEST_ERRORF("t marks everything when not all marked", "marked_count = %d, want 3", out.marked_count);
    }
}

static void test_toggle_mark_all_clears_everything_when_all_marked(void)
{
    Model in = make_nav_model(3, 1);
    in.mode = MODE_SELECT;
    in.marked[0] = 1;
    in.marked[1] = 1;
    in.marked[2] = 1;
    in.marked_count = 3;
    strcpy(in.marked_dir, in.current_path);
    Msg msg = { .type = MSG_TOGGLE_MARK_ALL };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    for (int i = 0; i < 3; i++) {
        if (out.marked[i]) {
            TEST_ERRORF("t clears everything when all marked", "marked[%d] = %d, want 0", i, out.marked[i]);
        }
    }
    if (out.marked_count != 0) {
        TEST_ERRORF("t clears everything when all marked", "marked_count = %d, want 0", out.marked_count);
    }
}

static void test_leave_select_mode_clears_marks(void)
{
    typedef struct {
        const char *label;
        MsgType msg_type;
    } Case;

    Case cases[] = {
        {"v clears marks on leaving select mode", MSG_TOGGLE_SELECT_MODE},
        {"Esc clears marks on leaving select mode", MSG_CANCEL},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        in.mode = MODE_SELECT;
        in.marked[0] = 1;
        in.marked[2] = 1;
        in.marked_count = 2;
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.marked_count != 0) {
            TEST_ERRORF(cases[i].label, "marked_count = %d, want 0", out.marked_count);
        }
        for (int j = 0; j < 3; j++) {
            if (out.marked[j]) {
                TEST_ERRORF(cases[i].label, "marked[%d] = %d, want 0", j, out.marked[j]);
            }
        }
    }
}

static void test_range_select_from_unmarked_anchor_marks_run_while_moving_down(void)
{
    Model in = make_nav_model(5, 1);
    in.mode = MODE_SELECT;
    Msg start_msg = { .type = MSG_TOGGLE_RANGE_SELECT };
    Model after_start;
    Cmd cmd;

    update(&start_msg, &in, &after_start, &cmd);

    if (!after_start.range_active) {
        TEST_ERRORF("r starts range from unmarked anchor", "range_active = %d, want 1", after_start.range_active);
    }
    if (!after_start.marked[1]) {
        TEST_ERRORF("r starts range from unmarked anchor", "marked[1] = %d, want 1 (anchor swept)", after_start.marked[1]);
    }
    if (after_start.marked_count != 1) {
        TEST_ERRORF("r starts range from unmarked anchor", "marked_count = %d, want 1", after_start.marked_count);
    }

    Msg down_msg = { .type = MSG_MOVE_DOWN };
    Model after_down1;
    update(&down_msg, &after_start, &after_down1, &cmd);

    if (after_down1.selected != 2) {
        TEST_ERRORF("r sweeps down", "selected = %d, want 2", after_down1.selected);
    }
    if (!after_down1.marked[2]) {
        TEST_ERRORF("r sweeps down", "marked[2] = %d, want 1", after_down1.marked[2]);
    }
    if (after_down1.marked_count != 2) {
        TEST_ERRORF("r sweeps down", "marked_count = %d, want 2", after_down1.marked_count);
    }

    Model after_down2;
    update(&down_msg, &after_down1, &after_down2, &cmd);

    if (after_down2.selected != 3) {
        TEST_ERRORF("r sweeps down further", "selected = %d, want 3", after_down2.selected);
    }
    if (!after_down2.marked[3]) {
        TEST_ERRORF("r sweeps down further", "marked[3] = %d, want 1", after_down2.marked[3]);
    }
    if (after_down2.marked_count != 3) {
        TEST_ERRORF("r sweeps down further", "marked_count = %d, want 3", after_down2.marked_count);
    }
    if (after_down2.marked[0] || after_down2.marked[4]) {
        TEST_ERRORF("r sweeps down further", "marked[0]=%d marked[4]=%d, want 0,0 (untouched)",
                    after_down2.marked[0], after_down2.marked[4]);
    }
}

static void test_range_select_from_marked_anchor_unmarks_run_while_moving_up(void)
{
    Model in = make_nav_model(5, 3);
    in.mode = MODE_SELECT;
    in.marked[0] = 1;
    in.marked[1] = 1;
    in.marked[2] = 1;
    in.marked[3] = 1;
    in.marked_count = 4;
    strcpy(in.marked_dir, in.current_path);
    Msg start_msg = { .type = MSG_TOGGLE_RANGE_SELECT };
    Model after_start;
    Cmd cmd;

    update(&start_msg, &in, &after_start, &cmd);

    if (!after_start.range_active) {
        TEST_ERRORF("r starts range from marked anchor", "range_active = %d, want 1", after_start.range_active);
    }
    if (after_start.marked[3]) {
        TEST_ERRORF("r starts range from marked anchor", "marked[3] = %d, want 0 (anchor swept to unmark)", after_start.marked[3]);
    }
    if (after_start.marked_count != 3) {
        TEST_ERRORF("r starts range from marked anchor", "marked_count = %d, want 3", after_start.marked_count);
    }

    Msg up_msg = { .type = MSG_MOVE_UP };
    Model after_up1;
    update(&up_msg, &after_start, &after_up1, &cmd);

    if (after_up1.selected != 2) {
        TEST_ERRORF("r sweeps up unmarking", "selected = %d, want 2", after_up1.selected);
    }
    if (after_up1.marked[2]) {
        TEST_ERRORF("r sweeps up unmarking", "marked[2] = %d, want 0", after_up1.marked[2]);
    }
    if (after_up1.marked_count != 2) {
        TEST_ERRORF("r sweeps up unmarking", "marked_count = %d, want 2", after_up1.marked_count);
    }
    if (!after_up1.marked[0] || !after_up1.marked[1]) {
        TEST_ERRORF("r sweeps up unmarking", "marked[0]=%d marked[1]=%d, want 1,1 (untouched)",
                    after_up1.marked[0], after_up1.marked[1]);
    }
}

static void test_range_select_backtracking_over_swept_entry_does_not_revert(void)
{
    Model in = make_nav_model(5, 0);
    in.mode = MODE_SELECT;
    Cmd cmd;

    Msg start_msg = { .type = MSG_TOGGLE_RANGE_SELECT };
    Model step;
    update(&start_msg, &in, &step, &cmd);

    Msg down_msg = { .type = MSG_MOVE_DOWN };
    Model tmp;
    update(&down_msg, &step, &tmp, &cmd);
    step = tmp;
    update(&down_msg, &step, &tmp, &cmd);
    step = tmp;

    if (step.selected != 2 || !step.marked[0] || !step.marked[1] || !step.marked[2]) {
        TEST_ERRORF("range sweeps 0..2 downward", "selected=%d marked[0..2]=%d,%d,%d, want 2,1,1,1",
                    step.selected, step.marked[0], step.marked[1], step.marked[2]);
    }

    Msg up_msg = { .type = MSG_MOVE_UP };
    update(&up_msg, &step, &tmp, &cmd);
    step = tmp;

    if (step.selected != 1 || !step.marked[1]) {
        TEST_ERRORF("moving back onto swept entry keeps it marked", "selected=%d marked[1]=%d, want 1,1",
                    step.selected, step.marked[1]);
    }
    if (!step.marked[2]) {
        TEST_ERRORF("backtracking does not revert entries left behind", "marked[2] = %d, want 1", step.marked[2]);
    }
    if (step.marked_count != 3) {
        TEST_ERRORF("backtracking does not change marked_count", "marked_count = %d, want 3", step.marked_count);
    }
}

static void test_space_toggles_independently_while_range_active(void)
{
    Model in = make_nav_model(5, 2);
    in.mode = MODE_SELECT;
    in.marked[0] = 1;
    in.marked[1] = 1;
    in.marked[2] = 1;
    in.marked[3] = 1;
    in.marked[4] = 1;
    in.marked_count = 5;
    strcpy(in.marked_dir, in.current_path);
    Cmd cmd;

    Msg start_msg = { .type = MSG_TOGGLE_RANGE_SELECT };
    Model step;
    update(&start_msg, &in, &step, &cmd);

    if (!step.range_active || step.marked[2] || step.marked_count != 4) {
        TEST_ERRORF("range starts unmarking from marked anchor", "range_active=%d marked[2]=%d marked_count=%d",
                    step.range_active, step.marked[2], step.marked_count);
    }

    Msg down_msg = { .type = MSG_MOVE_DOWN };
    Model tmp;
    update(&down_msg, &step, &tmp, &cmd);
    step = tmp;

    if (step.selected != 3 || step.marked[3] || step.marked_count != 3) {
        TEST_ERRORF("range sweeps unmark to index 3", "selected=%d marked[3]=%d marked_count=%d",
                    step.selected, step.marked[3], step.marked_count);
    }

    Msg space_msg = { .type = MSG_TOGGLE_MARK };
    update(&space_msg, &step, &tmp, &cmd);
    step = tmp;

    if (!step.marked[3] || step.marked_count != 4) {
        TEST_ERRORF("space independently re-marks entry 3", "marked[3]=%d marked_count=%d", step.marked[3], step.marked_count);
    }
    if (!step.range_active || step.range_target != 0) {
        TEST_ERRORF("range stays active and unaffected by space", "range_active=%d range_target=%d",
                    step.range_active, step.range_target);
    }

    update(&down_msg, &step, &tmp, &cmd);
    step = tmp;

    if (step.selected != 4 || step.marked[4] || step.marked_count != 3) {
        TEST_ERRORF("sweep continues to index 4 after space correction", "selected=%d marked[4]=%d marked_count=%d",
                    step.selected, step.marked[4], step.marked_count);
    }
    if (!step.marked[3]) {
        TEST_ERRORF("space-corrected entry 3 is not touched again by the sweep", "marked[3] = %d, want 1", step.marked[3]);
    }
    if (!step.marked[1]) {
        TEST_ERRORF("entry never visited by sweep or space stays marked", "marked[1] = %d, want 1", step.marked[1]);
    }
}

static void test_second_range_select_press_stops_extend_and_keeps_marks(void)
{
    Model in = make_nav_model(5, 0);
    in.mode = MODE_SELECT;
    Cmd cmd;

    Msg start_msg = { .type = MSG_TOGGLE_RANGE_SELECT };
    Model step;
    update(&start_msg, &in, &step, &cmd);

    Msg down_msg = { .type = MSG_MOVE_DOWN };
    Model tmp;
    update(&down_msg, &step, &tmp, &cmd);
    step = tmp;
    update(&down_msg, &step, &tmp, &cmd);
    step = tmp;

    update(&start_msg, &step, &tmp, &cmd);
    step = tmp;

    if (step.range_active) {
        TEST_ERRORF("second r press ends the range extend", "range_active = %d, want 0", step.range_active);
    }
    if (step.mode != MODE_SELECT) {
        TEST_ERRORF("second r press returns to plain MODE_SELECT", "mode = %d, want MODE_SELECT", step.mode);
    }
    if (!step.marked[0] || !step.marked[1] || !step.marked[2] || step.marked_count != 3) {
        TEST_ERRORF("marks applied during the sweep stay after stopping",
                    "marked[0..2]=%d,%d,%d marked_count=%d, want 1,1,1,3",
                    step.marked[0], step.marked[1], step.marked[2], step.marked_count);
    }

    update(&down_msg, &step, &tmp, &cmd);
    step = tmp;

    if (step.selected != 3 || step.marked[3]) {
        TEST_ERRORF("moving after the range stopped no longer sweeps", "selected=%d marked[3]=%d, want 3,0",
                    step.selected, step.marked[3]);
    }
}

static void test_go_parent_while_select_mode_keeps_marks_and_mode(void)
{
    Model in = make_nav_model(3, 1);
    in.mode = MODE_SELECT;
    in.marked[0] = 1;
    in.marked_count = 1;
    strcpy(in.marked_dir, in.current_path);
    Msg msg = { .type = MSG_GO_PARENT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_LOAD_DIR) {
        TEST_ERRORF("go parent in select mode issues load", "cmd.type = %d, want CMD_LOAD_DIR", cmd.type);
    }
    if (out.mode != MODE_SELECT) {
        TEST_ERRORF("go parent in select mode keeps mode", "mode = %d, want MODE_SELECT", out.mode);
    }
    if (out.marked_count != 1 || !out.marked[0]) {
        TEST_ERRORF("go parent in select mode keeps marks", "marked_count = %d marked[0] = %d, want 1,1",
                    out.marked_count, out.marked[0]);
    }

    Msg loaded_msg = { .type = MSG_DIR_LOADED };
    strcpy(loaded_msg.dir_loaded.path, "/home");
    loaded_msg.dir_loaded.entry_count = 0;
    Model out2;
    update(&loaded_msg, &out, &out2, &cmd);

    if (out2.mode != MODE_SELECT) {
        TEST_ERRORF("dir loaded after go parent keeps select mode", "mode = %d, want MODE_SELECT", out2.mode);
    }
    if (out2.marked_count != 1 || !out2.marked[0]) {
        TEST_ERRORF("dir loaded after go parent keeps marks off-screen", "marked_count = %d marked[0] = %d, want 1,1",
                    out2.marked_count, out2.marked[0]);
    }
    if (strcmp(out2.current_path, "/home") != 0) {
        TEST_ERRORF("dir loaded after go parent updates current_path", "current_path = %s, want /home", out2.current_path);
    }
}

static void test_activate_into_subdir_while_select_mode_keeps_marks_and_mode(void)
{
    Model in = make_model_with_entry("/home/user", "sub", S_IFDIR | 0755, 0);
    in.mode = MODE_SELECT;
    in.marked[0] = 1;
    in.marked_count = 1;
    strcpy(in.marked_dir, in.current_path);
    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_LOAD_DIR || strcmp(cmd.path, "/home/user/sub") != 0) {
        TEST_ERRORF("activate subdir in select mode issues load", "cmd = {%d, %s}, want {CMD_LOAD_DIR, /home/user/sub}",
                    cmd.type, cmd.path);
    }
    if (out.mode != MODE_SELECT) {
        TEST_ERRORF("activate subdir in select mode keeps mode", "mode = %d, want MODE_SELECT", out.mode);
    }
    if (out.marked_count != 1 || !out.marked[0]) {
        TEST_ERRORF("activate subdir in select mode keeps marks", "marked_count = %d marked[0] = %d, want 1,1",
                    out.marked_count, out.marked[0]);
    }
}

static void test_marking_in_new_directory_discards_old_marks(void)
{
    Model in = make_nav_model(3, 2);
    in.mode = MODE_SELECT;
    strcpy(in.marked_dir, "/tmp/old");
    in.marked[0] = 1;
    in.marked_count = 1;
    Msg msg = { .type = MSG_TOGGLE_MARK };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.marked[0]) {
        TEST_ERRORF("marking in new dir discards old marks", "marked[0] = %d, want 0", out.marked[0]);
    }
    if (!out.marked[2] || out.marked_count != 1) {
        TEST_ERRORF("marking in new dir starts fresh with the interacted entry",
                    "marked[2] = %d marked_count = %d, want 1,1", out.marked[2], out.marked_count);
    }
    if (strcmp(out.marked_dir, "/tmp") != 0) {
        TEST_ERRORF("marking in new dir records the new owning directory",
                    "marked_dir = %s, want /tmp", out.marked_dir);
    }
}

static void test_range_select_in_new_directory_discards_old_marks(void)
{
    Model in = make_nav_model(3, 2);
    in.mode = MODE_SELECT;
    strcpy(in.marked_dir, "/tmp/old");
    in.marked[0] = 1;
    in.marked_count = 1;
    Msg msg = { .type = MSG_TOGGLE_RANGE_SELECT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.marked[0]) {
        TEST_ERRORF("range select in new dir discards old marks", "marked[0] = %d, want 0", out.marked[0]);
    }
    if (!out.range_active || !out.marked[2] || out.marked_count != 1) {
        TEST_ERRORF("range select in new dir anchors fresh on the interacted entry",
                    "range_active=%d marked[2]=%d marked_count=%d, want 1,1,1",
                    out.range_active, out.marked[2], out.marked_count);
    }
    if (strcmp(out.marked_dir, "/tmp") != 0) {
        TEST_ERRORF("range select in new dir records the new owning directory",
                    "marked_dir = %s, want /tmp", out.marked_dir);
    }
}

static void test_mark_all_in_new_directory_discards_old_marks(void)
{
    Model in = make_nav_model(3, 2);
    in.mode = MODE_SELECT;
    strcpy(in.marked_dir, "/tmp/old");
    in.marked[0] = 1;
    in.marked_count = 1;
    Msg msg = { .type = MSG_TOGGLE_MARK_ALL };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.marked_count != 3 || !out.marked[0] || !out.marked[1] || !out.marked[2]) {
        TEST_ERRORF("mark-all in new dir starts fresh and marks everything",
                    "marked_count=%d marked[0..2]=%d,%d,%d, want 3,1,1,1",
                    out.marked_count, out.marked[0], out.marked[1], out.marked[2]);
    }
    if (strcmp(out.marked_dir, "/tmp") != 0) {
        TEST_ERRORF("mark-all in new dir records the new owning directory",
                    "marked_dir = %s, want /tmp", out.marked_dir);
    }
}

static void test_sort_group_filter_glob_are_noop_in_select_mode_with_marks(void)
{
    typedef struct {
        const char *label;
        MsgType msg_type;
    } Case;

    Case cases[] = {
        {"sort is a no-op with active marks", MSG_CYCLE_SORT},
        {"group is a no-op with active marks", MSG_CYCLE_GROUP},
        {"filter-plain is a no-op with active marks", MSG_FILTER_PLAIN},
        {"filter-regex is a no-op with active marks", MSG_FILTER_REGEX},
        {"glob-plain is a no-op with active marks", MSG_GLOB_PLAIN},
        {"glob-regex is a no-op with active marks", MSG_GLOB_REGEX},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        in.mode = MODE_SELECT;
        in.marked[0] = 1;
        in.marked_count = 1;
        strcpy(in.marked_dir, in.current_path);
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != MODE_SELECT) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_SELECT (unchanged)", out.mode);
        }
        if (out.marked_count != 1 || !out.marked[0]) {
            TEST_ERRORF(cases[i].label, "marked_count = %d marked[0] = %d, want 1,1 (unchanged)",
                        out.marked_count, out.marked[0]);
        }
        if (out.sort_mode != in.sort_mode || out.group_mode != in.group_mode) {
            TEST_ERRORF(cases[i].label, "sort_mode/group_mode changed, want unchanged");
        }
        if (out.filter_type != in.filter_type || out.glob_type != in.glob_type) {
            TEST_ERRORF(cases[i].label, "filter_type/glob_type changed, want unchanged");
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static void test_sort_works_in_select_mode_without_marks(void)
{
    Model in = make_nav_model(3, 1);
    in.mode = MODE_SELECT;
    Msg msg = { .type = MSG_CYCLE_SORT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_SELECT) {
        TEST_ERRORF("sort without marks stays in select mode", "mode = %d, want MODE_SELECT", out.mode);
    }
    if (out.sort_mode == in.sort_mode) {
        TEST_ERRORF("sort without marks still cycles", "sort_mode = %d, want changed from %d", out.sort_mode, in.sort_mode);
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

static void test_glob_commit_inside_archive_populates_entries_directly(void)
{
    ArchiveMember members[] = {
        make_archive_member("readme.txt", 0, 6),
        make_archive_member("src", 1, 0),
        make_archive_member("src/main.c", 0, 42),
        make_archive_member("src/lib", 1, 0),
        make_archive_member("src/lib/util.c", 0, 12),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "project.zip",
                                         "/home/user/project.zip");
    in.mode = MODE_GLOB;
    in.glob_type = GLOB_PLAIN;
    strcpy(in.edit_buf, ".c");
    in.edit_len = strlen(in.edit_buf);

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("glob commit inside archive", "cmd.type = %d, want CMD_NONE (no real-filesystem walk)", cmd.type);
    }
    if (out.mode != MODE_NAV) {
        TEST_ERRORF("glob commit inside archive", "mode = %d, want MODE_NAV", out.mode);
    }
    if (out.entry_count != 2 || strcmp(out.entries[0].name, "src/lib/util.c") != 0 ||
        strcmp(out.entries[1].name, "src/main.c") != 0) {
        TEST_ERRORF("glob commit inside archive",
                    "entries = [%s, %s] (%d), want [src/lib/util.c, src/main.c] (2)",
                    out.entries[0].name, out.entries[1].name, out.entry_count);
    }
    if (out.glob_capped) {
        TEST_ERRORF("glob commit inside archive", "glob_capped = 1, want 0");
    }

    free(out.archive_stack[0].members);
}

static void test_glob_commit_inside_archive_caps_and_reports_truncation(void)
{
    static ArchiveMember members[MAX_ENTRIES + 5];
    int member_count = MAX_ENTRIES + 5;
    for (int i = 0; i < member_count; i++) {
        char name[32];
        snprintf(name, sizeof(name), "file%d.txt", i);
        members[i] = make_archive_member(name, 0, i);
    }

    Model in = make_archive_level_model(members, member_count, "", "project.zip",
                                         "/home/user/project.zip");
    in.mode = MODE_GLOB;
    in.glob_type = GLOB_PLAIN;
    strcpy(in.edit_buf, ".txt");
    in.edit_len = strlen(in.edit_buf);

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.entry_count != MAX_ENTRIES) {
        TEST_ERRORF("glob commit inside archive caps", "entry_count = %d, want %d", out.entry_count, MAX_ENTRIES);
    }
    if (!out.glob_capped) {
        TEST_ERRORF("glob commit inside archive caps", "glob_capped = 0, want 1");
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("glob commit inside archive caps", "cmd.type = %d, want CMD_NONE", cmd.type);
    }

    free(out.archive_stack[0].members);
}

static void test_glob_commit_inside_archive_subfolder_uses_relative_names(void)
{
    ArchiveMember members[] = {
        make_archive_member("proj", 1, 0),
        make_archive_member("proj/src", 1, 0),
        make_archive_member("proj/src/main.c", 0, 42),
        make_archive_member("proj/readme.txt", 0, 6),
        make_archive_member("other.txt", 0, 3),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "proj", "project.zip",
                                         "/home/user/project.zip/proj");
    in.mode = MODE_GLOB;
    in.glob_type = GLOB_PLAIN;
    in.edit_buf[0] = '\0';
    in.edit_len = 0;

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.entry_count != 3 || strcmp(out.entries[0].name, "src") != 0 ||
        strcmp(out.entries[1].name, "readme.txt") != 0 || strcmp(out.entries[2].name, "src/main.c") != 0) {
        TEST_ERRORF("glob commit relative names",
                    "entries = [%s, %s, %s] (%d), want [src, readme.txt, src/main.c] (3)",
                    out.entries[0].name, out.entries[1].name, out.entries[2].name, out.entry_count);
    }

    free(out.archive_stack[0].members);
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

static History make_recall_history_fixture(void)
{
    History h = history_create();
    history_record_command(&h, "/tmp", "ls");
    history_record_command(&h, "/tmp", "git status");
    history_record_command(&h, "/tmp", "make test");
    return h;
}

static Model make_recall_model(History *h, const char *edit_buf, int recall_cursor,
                                const char *stash, size_t stash_len)
{
    Model m = make_edit_model(MODE_RUN_CMD, edit_buf, 1, 3);
    m.history = h;
    m.recall_cursor = recall_cursor;
    strcpy(m.recall_stash, stash);
    m.recall_stash_len = stash_len;
    return m;
}

static void test_recall_prev_first_press_recalls_most_recent_and_stashes_draft(void)
{
    History h = make_recall_history_fixture();
    Model in = make_recall_model(&h, "draft", 0, "", 0);
    Msg msg = { .type = MSG_RECALL_PREV };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.edit_buf, "!make test") != 0)
        TEST_ERRORF("recall prev first press", "edit_buf = '%s', want '!make test'", out.edit_buf);
    if (out.edit_len != strlen("!make test"))
        TEST_ERRORF("recall prev first press", "edit_len = %zu, want %zu", out.edit_len, strlen("!make test"));
    if (out.recall_cursor != 1)
        TEST_ERRORF("recall prev first press", "recall_cursor = %d, want 1", out.recall_cursor);
    if (strcmp(out.recall_stash, "draft") != 0)
        TEST_ERRORF("recall prev first press", "recall_stash = '%s', want 'draft'", out.recall_stash);
    if (out.recall_stash_len != strlen("draft"))
        TEST_ERRORF("recall prev first press", "recall_stash_len = %zu, want %zu", out.recall_stash_len, strlen("draft"));

    history_free(&h);
}

static void test_recall_prev_repeated_walks_older_and_clamps_at_oldest(void)
{
    History h = make_recall_history_fixture();

    Model second = make_recall_model(&h, "!make test", 1, "draft", 5);
    Msg msg = { .type = MSG_RECALL_PREV };
    Model out;
    Cmd cmd;

    update(&msg, &second, &out, &cmd);

    if (strcmp(out.edit_buf, "!git status") != 0)
        TEST_ERRORF("recall prev second press", "edit_buf = '%s', want '!git status'", out.edit_buf);
    if (out.recall_cursor != 2)
        TEST_ERRORF("recall prev second press", "recall_cursor = %d, want 2", out.recall_cursor);

    Model third = make_recall_model(&h, "!git status", 2, "draft", 5);
    update(&msg, &third, &out, &cmd);

    if (strcmp(out.edit_buf, "!ls") != 0)
        TEST_ERRORF("recall prev third press (oldest)", "edit_buf = '%s', want '!ls'", out.edit_buf);
    if (out.recall_cursor != 3)
        TEST_ERRORF("recall prev third press (oldest)", "recall_cursor = %d, want 3", out.recall_cursor);

    Model at_oldest = make_recall_model(&h, "!ls", 3, "draft", 5);
    update(&msg, &at_oldest, &out, &cmd);

    if (strcmp(out.edit_buf, "!ls") != 0)
        TEST_ERRORF("recall prev clamps at oldest", "edit_buf = '%s', want '!ls' (unchanged)", out.edit_buf);
    if (out.recall_cursor != 3)
        TEST_ERRORF("recall prev clamps at oldest", "recall_cursor = %d, want 3 (unchanged)", out.recall_cursor);

    history_free(&h);
}

static void test_recall_next_walks_toward_newest(void)
{
    History h = make_recall_history_fixture();
    Model in = make_recall_model(&h, "!ls", 3, "draft", 5);
    Msg msg = { .type = MSG_RECALL_NEXT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.edit_buf, "!git status") != 0)
        TEST_ERRORF("recall next", "edit_buf = '%s', want '!git status'", out.edit_buf);
    if (out.recall_cursor != 2)
        TEST_ERRORF("recall next", "recall_cursor = %d, want 2", out.recall_cursor);

    history_free(&h);
}

static void test_recall_next_past_newest_restores_stash_and_resets(void)
{
    History h = make_recall_history_fixture();
    Model in = make_recall_model(&h, "!make test", 1, "draft", 5);
    Msg msg = { .type = MSG_RECALL_NEXT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (strcmp(out.edit_buf, "draft") != 0)
        TEST_ERRORF("recall next past newest", "edit_buf = '%s', want 'draft'", out.edit_buf);
    if (out.edit_len != 5)
        TEST_ERRORF("recall next past newest", "edit_len = %zu, want 5", out.edit_len);
    if (out.recall_cursor != 0)
        TEST_ERRORF("recall next past newest", "recall_cursor = %d, want 0 (not cycling)", out.recall_cursor);

    history_free(&h);
}

static void test_recall_prev_and_next_on_zero_history_folder_is_noop(void)
{
    History h = history_create();
    Model prev_in = make_recall_model(&h, "draft", 0, "", 0);
    Msg prev_msg = { .type = MSG_RECALL_PREV };
    Model out;
    Cmd cmd;

    update(&prev_msg, &prev_in, &out, &cmd);

    if (strcmp(out.edit_buf, "draft") != 0)
        TEST_ERRORF("recall prev zero history", "edit_buf = '%s', want 'draft' (unchanged)", out.edit_buf);
    if (out.recall_cursor != 0)
        TEST_ERRORF("recall prev zero history", "recall_cursor = %d, want 0 (unchanged)", out.recall_cursor);

    Model next_in = make_recall_model(&h, "draft", 0, "", 0);
    Msg next_msg = { .type = MSG_RECALL_NEXT };
    update(&next_msg, &next_in, &out, &cmd);

    if (strcmp(out.edit_buf, "draft") != 0)
        TEST_ERRORF("recall next zero history", "edit_buf = '%s', want 'draft' (unchanged)", out.edit_buf);
    if (out.recall_cursor != 0)
        TEST_ERRORF("recall next zero history", "recall_cursor = %d, want 0 (unchanged)", out.recall_cursor);

    history_free(&h);
}

static void test_recall_is_scoped_to_mode_run_cmd(void)
{
    History h = make_recall_history_fixture();
    AppMode other_modes[] = { MODE_FILTER, MODE_GLOB, MODE_RENAME, MODE_CREATE };

    for (size_t i = 0; i < sizeof(other_modes) / sizeof(other_modes[0]); i++) {
        Model in = make_recall_model(&h, "draft", 0, "", 0);
        in.mode = other_modes[i];
        Msg msg = { .type = MSG_RECALL_PREV };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (strcmp(out.edit_buf, "draft") != 0)
            TEST_ERRORF("recall scoped to run cmd", "mode %d: edit_buf = '%s', want 'draft' (unchanged)",
                        other_modes[i], out.edit_buf);
        if (out.recall_cursor != 0)
            TEST_ERRORF("recall scoped to run cmd", "mode %d: recall_cursor = %d, want 0 (unchanged)",
                        other_modes[i], out.recall_cursor);
    }

    history_free(&h);
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
    strcpy(in.yank_paths[0], "/tmp/old.txt");
    in.yank_count = 1;
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.yank_count != 0) {
        TEST_ERRORF("rename clears matching yank", "yank_count = %d, want 0", out.yank_count);
    }
}

static void test_rename_leaves_yank_for_different_entry(void)
{
    Model in = make_edit_model(MODE_RENAME, "new.txt", 1, 3);
    strcpy(in.current_path, "/tmp");
    strcpy(in.entries[1].name, "old.txt");
    strcpy(in.yank_paths[0], "/tmp/other.txt");
    in.yank_count = 1;
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_ACTIVATE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.yank_count != 1 || strcmp(out.yank_paths[0], "/tmp/other.txt") != 0) {
        TEST_ERRORF("rename leaves unrelated yank", "yank = {count=%d, [0]=%s}, want {1, /tmp/other.txt}",
                    out.yank_count, out.yank_paths[0]);
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
    strcpy(in.yank_paths[0], "/tmp/target");
    in.yank_count = 1;
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 'y' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.yank_count != 0) {
        TEST_ERRORF("confirm delete clears matching yank", "yank_count = %d, want 0", out.yank_count);
    }
}

static void test_confirm_delete_leaves_yank_for_different_entry(void)
{
    Model in = make_confirm_delete_model("target", S_IFREG | 0644, 1, 3, 0);
    strcpy(in.yank_paths[0], "/tmp/other.txt");
    in.yank_count = 1;
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 'y' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.yank_count != 1 || strcmp(out.yank_paths[0], "/tmp/other.txt") != 0) {
        TEST_ERRORF("confirm delete leaves unrelated yank", "yank = {count=%d, [0]=%s}, want {1, /tmp/other.txt}",
                    out.yank_count, out.yank_paths[0]);
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

static void test_batch_delete_with_marks_triggers_combined_confirm(void)
{
    typedef struct {
        const char *label;
        MsgType msg_type;
        int expected_permanent;
    } Case;

    Case cases[] = {
        {"backspace with marks asks combined confirm", MSG_DELETE, 0},
        {"x with marks asks combined confirm", MSG_DELETE_PERMANENT, 1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        in.mode = MODE_SELECT;
        in.marked[0] = 1;
        in.marked[2] = 1;
        in.marked_count = 2;
        strcpy(in.marked_dir, in.current_path);
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != MODE_CONFIRM_DELETE) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_CONFIRM_DELETE", out.mode);
        }
        if (out.confirm_permanent_delete != cases[i].expected_permanent) {
            TEST_ERRORF(cases[i].label, "confirm_permanent_delete = %d, want %d",
                        out.confirm_permanent_delete, cases[i].expected_permanent);
        }
        if (out.marked_count != 2) {
            TEST_ERRORF(cases[i].label, "marked_count = %d, want 2 (marks not yet cleared)", out.marked_count);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
    }
}

static void test_delete_in_select_mode_with_zero_marks_targets_cursor(void)
{
    typedef struct {
        const char *label;
        MsgType msg_type;
        int expected_permanent;
    } Case;

    Case cases[] = {
        {"backspace with no marks targets cursor entry", MSG_DELETE, 0},
        {"x with no marks targets cursor entry", MSG_DELETE_PERMANENT, 1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        in.mode = MODE_SELECT;
        strcpy(in.entries[1].name, "file.txt");
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != MODE_CONFIRM_DELETE) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_CONFIRM_DELETE", out.mode);
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

static void test_delete_outside_select_mode_ignores_stray_marks(void)
{
    Model in = make_nav_model(3, 1);
    strcpy(in.entries[1].name, "file.txt");
    in.marked[0] = 1;
    in.marked_count = 1;
    strcpy(in.marked_dir, in.current_path);
    Msg msg = { .type = MSG_DELETE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_CONFIRM_DELETE) {
        TEST_ERRORF("delete outside select mode is single-entry", "mode = %d, want MODE_CONFIRM_DELETE", out.mode);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("delete outside select mode issues no cmd yet", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
}

static Model make_batch_confirm_delete_model(int confirm_permanent)
{
    Model m = make_nav_model(3, 1);
    m.mode = MODE_CONFIRM_DELETE;
    m.confirm_permanent_delete = confirm_permanent;
    strcpy(m.current_path, "/tmp");
    strcpy(m.marked_dir, "/tmp");
    strcpy(m.entries[0].name, "a.txt");
    m.entries[0].st.st_mode = S_IFREG | 0644;
    strcpy(m.entries[1].name, "b.txt");
    m.entries[1].st.st_mode = S_IFREG | 0644;
    strcpy(m.entries[2].name, "subdir");
    m.entries[2].st.st_mode = S_IFDIR | 0755;
    m.marked[0] = 1;
    m.marked[2] = 1;
    m.marked_count = 2;
    strcpy(m.marked_items[0].path, "/tmp/a.txt");
    m.marked_items[0].is_dir = 0;
    strcpy(m.marked_items[1].path, "/tmp/subdir");
    m.marked_items[1].is_dir = 1;
    m.range_active = 1;
    return m;
}

static void test_confirm_batch_delete_yes_builds_batch_cmd_and_clears_marks(void)
{
    typedef struct {
        const char *label;
        int confirm_permanent;
        CmdType expected_cmd_type;
    } Case;

    Case cases[] = {
        {"confirming batch trash", 0, CMD_TRASH},
        {"confirming batch permanent delete", 1, CMD_DELETE},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_batch_confirm_delete_model(cases[i].confirm_permanent);
        Msg msg = { .type = MSG_TEXT_INPUT, .ch = 'y' };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != cases[i].expected_cmd_type) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want %d", cmd.type, cases[i].expected_cmd_type);
        }
        if (cmd.batch_count != 2) {
            TEST_ERRORF(cases[i].label, "cmd.batch_count = %d, want 2", cmd.batch_count);
        }
        if (strcmp(cmd.batch_items[0].path, "/tmp/a.txt") != 0 || cmd.batch_items[0].is_dir != 0) {
            TEST_ERRORF(cases[i].label, "batch_items[0] = {%s, is_dir=%d}, want {/tmp/a.txt, is_dir=0}",
                        cmd.batch_items[0].path, cmd.batch_items[0].is_dir);
        }
        if (strcmp(cmd.batch_items[1].path, "/tmp/subdir") != 0 || cmd.batch_items[1].is_dir != 1) {
            TEST_ERRORF(cases[i].label, "batch_items[1] = {%s, is_dir=%d}, want {/tmp/subdir, is_dir=1}",
                        cmd.batch_items[1].path, cmd.batch_items[1].is_dir);
        }
        if (out.mode != MODE_NAV) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_NAV", out.mode);
        }
        if (out.marked_count != 0 || out.marked[0] || out.marked[2] || out.range_active) {
            TEST_ERRORF(cases[i].label,
                        "marked_count=%d marked[0]=%d marked[2]=%d range_active=%d, want 0,0,0,0",
                        out.marked_count, out.marked[0], out.marked[2], out.range_active);
        }
        if (out.marked_dir[0] != '\0') {
            TEST_ERRORF(cases[i].label, "marked_dir = '%s', want cleared", out.marked_dir);
        }
    }
}

static void test_confirm_batch_delete_no_leaves_marks_untouched(void)
{
    Model in = make_batch_confirm_delete_model(0);
    Msg msg = { .type = MSG_TEXT_INPUT, .ch = 'n' };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("declining a batch delete issues no cmd", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
    if (out.marked_count != 2 || !out.marked[0] || !out.marked[2]) {
        TEST_ERRORF("declining a batch delete leaves marks untouched",
                    "marked_count=%d marked[0]=%d marked[2]=%d, want 2,1,1",
                    out.marked_count, out.marked[0], out.marked[2]);
    }
}

static void test_confirm_batch_delete_targets_marks_directory_not_currently_displayed_dir(void)
{
    Model dir_a = make_nav_model(3, 0);
    dir_a.mode = MODE_SELECT;
    strcpy(dir_a.current_path, "/dirA");
    strcpy(dir_a.entries[0].name, "keep.txt");
    dir_a.entries[0].st.st_mode = S_IFREG | 0644;
    strcpy(dir_a.entries[1].name, "mid.txt");
    dir_a.entries[1].st.st_mode = S_IFREG | 0644;
    strcpy(dir_a.entries[2].name, "target-dir");
    dir_a.entries[2].st.st_mode = S_IFDIR | 0755;

    Msg mark_msg = { .type = MSG_TOGGLE_MARK };
    Model after_mark0;
    Cmd cmd;
    update(&mark_msg, &dir_a, &after_mark0, &cmd);

    Model before_mark2 = after_mark0;
    before_mark2.selected = 2;
    Model after_mark2;
    update(&mark_msg, &before_mark2, &after_mark2, &cmd);

    if (after_mark2.marked_count != 2 || strcmp(after_mark2.marked_dir, "/dirA") != 0) {
        TEST_ERRORF("setup: marks placed in dirA", "marked_count=%d marked_dir=%s, want 2,/dirA",
                    after_mark2.marked_count, after_mark2.marked_dir);
    }

    static Entry dir_b_entries[MAX_ENTRIES];
    strcpy(dir_b_entries[0].name, "zz-other-dir");
    dir_b_entries[0].st.st_mode = S_IFDIR | 0755;
    strcpy(dir_b_entries[1].name, "zz-other1.txt");
    dir_b_entries[1].st.st_mode = S_IFREG | 0644;
    strcpy(dir_b_entries[2].name, "zz-other2.txt");
    dir_b_entries[2].st.st_mode = S_IFREG | 0644;

    Msg loaded_msg = { .type = MSG_DIR_LOADED };
    loaded_msg.dir_loaded.entries = dir_b_entries;
    loaded_msg.dir_loaded.entry_count = 3;
    strcpy(loaded_msg.dir_loaded.path, "/dirB");
    Model in_dir_b;
    update(&loaded_msg, &after_mark2, &in_dir_b, &cmd);

    if (strcmp(in_dir_b.current_path, "/dirB") != 0 || strcmp(in_dir_b.marked_dir, "/dirA") != 0) {
        TEST_ERRORF("setup: now viewing dirB with marks still owned by dirA",
                    "current_path=%s marked_dir=%s, want /dirB,/dirA", in_dir_b.current_path, in_dir_b.marked_dir);
    }
    if (in_dir_b.marked_count != 2) {
        TEST_ERRORF("setup: marks survive the directory switch", "marked_count=%d, want 2", in_dir_b.marked_count);
    }

    Msg delete_msg = { .type = MSG_DELETE };
    Model confirming;
    update(&delete_msg, &in_dir_b, &confirming, &cmd);

    if (confirming.mode != MODE_CONFIRM_DELETE) {
        TEST_ERRORF("delete while viewing dirB asks combined confirm", "mode = %d, want MODE_CONFIRM_DELETE",
                    confirming.mode);
    }

    Msg confirm_msg = { .type = MSG_TEXT_INPUT, .ch = 'y' };
    Model out;
    update(&confirm_msg, &confirming, &out, &cmd);

    if (cmd.type != CMD_TRASH) {
        TEST_ERRORF("confirming batch delete off-screen targets dirA", "cmd.type = %d, want CMD_TRASH", cmd.type);
    }
    if (cmd.batch_count != 2) {
        TEST_ERRORF("confirming batch delete off-screen targets dirA", "cmd.batch_count = %d, want 2",
                    cmd.batch_count);
    }

    int found_keep = 0, found_target_dir = 0, found_wrong_dir_path = 0;
    for (int i = 0; i < cmd.batch_count; i++) {
        if (strcmp(cmd.batch_items[i].path, "/dirA/keep.txt") == 0 && cmd.batch_items[i].is_dir == 0)
            found_keep = 1;
        if (strcmp(cmd.batch_items[i].path, "/dirA/target-dir") == 0 && cmd.batch_items[i].is_dir == 1)
            found_target_dir = 1;
        if (strncmp(cmd.batch_items[i].path, "/dirA/zz-other", 15) == 0)
            found_wrong_dir_path = 1;
    }

    if (!found_keep || !found_target_dir) {
        TEST_ERRORF("confirming batch delete off-screen targets dirA",
                    "batch items = {%s, %s}, want /dirA/keep.txt and /dirA/target-dir",
                    cmd.batch_items[0].path, cmd.batch_items[1].path);
    }
    if (found_wrong_dir_path) {
        TEST_ERRORF("confirming batch delete off-screen never mixes in dirB's filenames under dirA's path",
                    "batch items = {%s, %s}", cmd.batch_items[0].path, cmd.batch_items[1].path);
    }
}

static Model make_batch_yank_model(void)
{
    Model m = make_nav_model(3, 1);
    m.mode = MODE_SELECT;
    strcpy(m.current_path, "/tmp");
    strcpy(m.marked_dir, "/tmp");
    strcpy(m.entries[0].name, "a.txt");
    m.entries[0].st.st_mode = S_IFREG | 0644;
    strcpy(m.entries[1].name, "b.txt");
    m.entries[1].st.st_mode = S_IFREG | 0644;
    strcpy(m.entries[2].name, "subdir");
    m.entries[2].st.st_mode = S_IFDIR | 0755;
    m.marked[0] = 1;
    m.marked[2] = 1;
    m.marked_count = 2;
    strcpy(m.marked_items[0].path, "/tmp/a.txt");
    m.marked_items[0].is_dir = 0;
    strcpy(m.marked_items[1].path, "/tmp/subdir");
    m.marked_items[1].is_dir = 1;
    m.range_active = 1;
    return m;
}

static void test_batch_yank_copy_and_move_capture_all_marked_paths(void)
{
    typedef struct {
        const char *label;
        MsgType msg_type;
        int expected_is_move;
    } Case;

    Case cases[] = {
        {"batch yank copy with marks captures all marked paths", MSG_YANK_COPY, 0},
        {"batch yank move with marks captures all marked paths", MSG_YANK_MOVE, 1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_batch_yank_model();
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.yank_count != 2) {
            TEST_ERRORF(cases[i].label, "yank_count = %d, want 2", out.yank_count);
            continue;
        }

        int found_a = 0, found_subdir = 0;
        for (int j = 0; j < out.yank_count; j++) {
            if (strcmp(out.yank_paths[j], "/tmp/a.txt") == 0)
                found_a = 1;
            if (strcmp(out.yank_paths[j], "/tmp/subdir") == 0)
                found_subdir = 1;
        }
        if (!found_a || !found_subdir) {
            TEST_ERRORF(cases[i].label, "yank_paths = {%s, %s}, want /tmp/a.txt and /tmp/subdir",
                        out.yank_paths[0], out.yank_paths[1]);
        }
        if (out.yank_is_move != cases[i].expected_is_move) {
            TEST_ERRORF(cases[i].label, "yank_is_move = %d, want %d", out.yank_is_move, cases[i].expected_is_move);
        }
        if (out.mode != MODE_NAV) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_NAV (batch yank leaves select mode)", out.mode);
        }
        if (out.marked_count != 0 || out.marked[0] || out.marked[2] || out.range_active) {
            TEST_ERRORF(cases[i].label,
                        "marked_count=%d marked[0]=%d marked[2]=%d range_active=%d, want 0,0,0,0 (marks cleared)",
                        out.marked_count, out.marked[0], out.marked[2], out.range_active);
        }
        if (out.marked_dir[0] != '\0') {
            TEST_ERRORF(cases[i].label, "marked_dir = '%s', want cleared", out.marked_dir);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE (yank issues no cmd)", cmd.type);
        }
    }
}

static void test_yank_in_select_mode_with_zero_marks_targets_cursor(void)
{
    typedef struct {
        const char *label;
        MsgType msg_type;
        int expected_is_move;
    } Case;

    Case cases[] = {
        {"yank copy in select mode with no marks targets cursor entry", MSG_YANK_COPY, 0},
        {"yank move in select mode with no marks targets cursor entry", MSG_YANK_MOVE, 1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        in.mode = MODE_SELECT;
        strcpy(in.entries[1].name, "file.txt");
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.yank_count != 1 || strcmp(out.yank_paths[0], "/tmp/file.txt") != 0) {
            TEST_ERRORF(cases[i].label, "yank = {count=%d, [0]=%s}, want {1, /tmp/file.txt}",
                        out.yank_count, out.yank_paths[0]);
        }
        if (out.yank_is_move != cases[i].expected_is_move) {
            TEST_ERRORF(cases[i].label, "yank_is_move = %d, want %d", out.yank_is_move, cases[i].expected_is_move);
        }
        if (out.mode != MODE_SELECT) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_SELECT (single-entry yank doesn't leave select mode)",
                        out.mode);
        }
    }
}

static void test_yank_outside_select_mode_ignores_stray_marks(void)
{
    Model in = make_nav_model(3, 1);
    strcpy(in.entries[1].name, "file.txt");
    in.marked[0] = 1;
    in.marked_count = 1;
    strcpy(in.marked_dir, in.current_path);

    Msg msg = { .type = MSG_YANK_COPY };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.yank_count != 1 || strcmp(out.yank_paths[0], "/tmp/file.txt") != 0) {
        TEST_ERRORF("yank outside select mode is single-entry", "yank = {count=%d, [0]=%s}, want {1, /tmp/file.txt}",
                    out.yank_count, out.yank_paths[0]);
    }
    if (out.marked_count != 1) {
        TEST_ERRORF("yank outside select mode leaves stray marks untouched", "marked_count = %d, want 1",
                    out.marked_count);
    }
}

static void test_batch_yank_targets_marks_directory_not_currently_displayed_dir(void)
{
    Model dir_a = make_nav_model(3, 0);
    dir_a.mode = MODE_SELECT;
    strcpy(dir_a.current_path, "/dirA");
    strcpy(dir_a.entries[0].name, "keep.txt");
    dir_a.entries[0].st.st_mode = S_IFREG | 0644;
    strcpy(dir_a.entries[1].name, "mid.txt");
    dir_a.entries[1].st.st_mode = S_IFREG | 0644;
    strcpy(dir_a.entries[2].name, "target-dir");
    dir_a.entries[2].st.st_mode = S_IFDIR | 0755;

    Msg mark_msg = { .type = MSG_TOGGLE_MARK };
    Model after_mark0;
    Cmd cmd;
    update(&mark_msg, &dir_a, &after_mark0, &cmd);

    Model before_mark2 = after_mark0;
    before_mark2.selected = 2;
    Model after_mark2;
    update(&mark_msg, &before_mark2, &after_mark2, &cmd);

    if (after_mark2.marked_count != 2 || strcmp(after_mark2.marked_dir, "/dirA") != 0) {
        TEST_ERRORF("setup: marks placed in dirA", "marked_count=%d marked_dir=%s, want 2,/dirA",
                    after_mark2.marked_count, after_mark2.marked_dir);
    }

    static Entry dir_b_entries[MAX_ENTRIES];
    strcpy(dir_b_entries[0].name, "zz-other-dir");
    dir_b_entries[0].st.st_mode = S_IFDIR | 0755;
    strcpy(dir_b_entries[1].name, "zz-other1.txt");
    dir_b_entries[1].st.st_mode = S_IFREG | 0644;
    strcpy(dir_b_entries[2].name, "zz-other2.txt");
    dir_b_entries[2].st.st_mode = S_IFREG | 0644;

    Msg loaded_msg = { .type = MSG_DIR_LOADED };
    loaded_msg.dir_loaded.entries = dir_b_entries;
    loaded_msg.dir_loaded.entry_count = 3;
    strcpy(loaded_msg.dir_loaded.path, "/dirB");
    Model in_dir_b;
    update(&loaded_msg, &after_mark2, &in_dir_b, &cmd);

    if (strcmp(in_dir_b.current_path, "/dirB") != 0 || strcmp(in_dir_b.marked_dir, "/dirA") != 0) {
        TEST_ERRORF("setup: now viewing dirB with marks still owned by dirA",
                    "current_path=%s marked_dir=%s, want /dirB,/dirA", in_dir_b.current_path, in_dir_b.marked_dir);
    }
    if (in_dir_b.marked_count != 2) {
        TEST_ERRORF("setup: marks survive the directory switch", "marked_count=%d, want 2", in_dir_b.marked_count);
    }

    Msg yank_msg = { .type = MSG_YANK_COPY };
    Model out;
    update(&yank_msg, &in_dir_b, &out, &cmd);

    if (out.yank_count != 2) {
        TEST_ERRORF("batch yank off-screen targets dirA", "yank_count = %d, want 2", out.yank_count);
    }

    int found_keep = 0, found_target_dir = 0, found_wrong_dir_path = 0;
    for (int i = 0; i < out.yank_count; i++) {
        if (strcmp(out.yank_paths[i], "/dirA/keep.txt") == 0)
            found_keep = 1;
        if (strcmp(out.yank_paths[i], "/dirA/target-dir") == 0)
            found_target_dir = 1;
        if (strncmp(out.yank_paths[i], "/dirA/zz-other", 15) == 0)
            found_wrong_dir_path = 1;
    }

    if (!found_keep || !found_target_dir) {
        TEST_ERRORF("batch yank off-screen targets dirA",
                    "yank_paths = {%s, %s}, want /dirA/keep.txt and /dirA/target-dir",
                    out.yank_paths[0], out.yank_paths[1]);
    }
    if (found_wrong_dir_path) {
        TEST_ERRORF("batch yank off-screen never mixes in dirB's filenames under dirA's path",
                    "yank_paths = {%s, %s}", out.yank_paths[0], out.yank_paths[1]);
    }
}

static void test_yank(void)
{
    typedef struct {
        const char *label;
        const char *entry_name;
        MsgType msg_type;
        int expected_yank_count;
        const char *expected_yank_path0;
        int expected_yank_is_move;
    } Case;

    Case cases[] = {
        {"yank copy on unprotected entry", "file.txt", MSG_YANK_COPY, 1, "/tmp/file.txt", 0},
        {"yank move on unprotected entry", "file.txt", MSG_YANK_MOVE, 1, "/tmp/file.txt", 1},
        {"yank copy is a no-op on '.'", ".", MSG_YANK_COPY, 0, "", 0},
        {"yank move is a no-op on '..'", "..", MSG_YANK_MOVE, 0, "", 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(3, 1);
        strcpy(in.entries[1].name, cases[i].entry_name);
        Msg msg = { .type = cases[i].msg_type };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.yank_count != cases[i].expected_yank_count) {
            TEST_ERRORF(cases[i].label, "yank_count = %d, want %d", out.yank_count, cases[i].expected_yank_count);
        }
        if (cases[i].expected_yank_count > 0 && strcmp(out.yank_paths[0], cases[i].expected_yank_path0) != 0) {
            TEST_ERRORF(cases[i].label, "yank_paths[0] = %s, want %s",
                        out.yank_paths[0], cases[i].expected_yank_path0);
        }
        if (out.yank_is_move != cases[i].expected_yank_is_move) {
            TEST_ERRORF(cases[i].label, "yank_is_move = %d, want %d",
                        out.yank_is_move, cases[i].expected_yank_is_move);
        }
        if (out.yank_from_archive != 0) {
            TEST_ERRORF(cases[i].label, "yank_from_archive = %d, want 0 (real-directory yank)",
                        out.yank_from_archive);
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
    strcpy(in.yank_paths[0], "/tmp/first.txt");
    in.yank_count = 1;
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_YANK_COPY };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.yank_count != 1 || strcmp(out.yank_paths[0], "/tmp/second.txt") != 0 || out.yank_is_move != 0) {
        TEST_ERRORF("re-yank replaces pending", "yank = {count=%d, [0]=%s, move=%d}, want {1, /tmp/second.txt, move=0}",
                    out.yank_count, out.yank_paths[0], out.yank_is_move);
    }
}

static void test_nav_cancel_clears_pending_yank(void)
{
    Model in = make_nav_model(3, 1);
    strcpy(in.yank_paths[0], "/tmp/first.txt");
    in.yank_count = 1;
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_CANCEL };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.yank_count != 0) {
        TEST_ERRORF("nav cancel clears yank", "yank_count = %d, want 0", out.yank_count);
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

    if (out.yank_count != 0) {
        TEST_ERRORF("nav cancel noop", "yank_count = %d, want still 0", out.yank_count);
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
        const char *expected_dest;
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
        strcpy(in.yank_paths[0], cases[i].yank_path);
        in.yank_count = 1;
        in.yank_is_move = cases[i].yank_is_move;

        Msg msg = { .type = MSG_PASTE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != cases[i].expected_cmd_type) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want %d", cmd.type, cases[i].expected_cmd_type);
            continue;
        }
        if (cmd.batch_count != 1) {
            TEST_ERRORF(cases[i].label, "cmd.batch_count = %d, want 1", cmd.batch_count);
            continue;
        }
        if (strcmp(cmd.batch_items[0].path, cases[i].yank_path) != 0) {
            TEST_ERRORF(cases[i].label, "cmd.batch_items[0].path = %s, want %s",
                        cmd.batch_items[0].path, cases[i].yank_path);
        }
        if (strcmp(cmd.batch_items[0].dest, cases[i].expected_dest) != 0) {
            TEST_ERRORF(cases[i].label, "cmd.batch_items[0].dest = %s, want %s",
                        cmd.batch_items[0].dest, cases[i].expected_dest);
        }
        if (out.yank_count != 0) {
            TEST_ERRORF(cases[i].label, "yank_count = %d, want cleared after paste", out.yank_count);
        }
    }
}

static void test_paste_pending_batch_replays_all_yanked_paths(void)
{
    Model in = make_nav_model(1, 0);
    strcpy(in.current_path, "/tmp");
    strcpy(in.entries[0].name, "placeholder");
    in.unfiltered_count = 0;

    strcpy(in.yank_paths[0], "/src/a.txt");
    strcpy(in.yank_paths[1], "/src/b.txt");
    strcpy(in.yank_paths[2], "/src/c.txt");
    in.yank_count = 3;
    in.yank_is_move = 0;

    Msg msg = { .type = MSG_PASTE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_COPY) {
        TEST_ERRORF("batch paste replays all yanked paths", "cmd.type = %d, want CMD_COPY", cmd.type);
    }
    if (cmd.batch_count != 3) {
        TEST_ERRORF("batch paste replays all yanked paths", "cmd.batch_count = %d, want 3", cmd.batch_count);
    }

    const char *expected_src[] = { "/src/a.txt", "/src/b.txt", "/src/c.txt" };
    const char *expected_dest[] = { "/tmp/a.txt", "/tmp/b.txt", "/tmp/c.txt" };
    for (int i = 0; i < cmd.batch_count && i < 3; i++) {
        if (strcmp(cmd.batch_items[i].path, expected_src[i]) != 0) {
            TEST_ERRORF("batch paste replays all yanked paths", "batch_items[%d].path = %s, want %s",
                        i, cmd.batch_items[i].path, expected_src[i]);
        }
        if (strcmp(cmd.batch_items[i].dest, expected_dest[i]) != 0) {
            TEST_ERRORF("batch paste replays all yanked paths", "batch_items[%d].dest = %s, want %s",
                        i, cmd.batch_items[i].dest, expected_dest[i]);
        }
    }
    if (out.yank_count != 0) {
        TEST_ERRORF("batch paste replays all yanked paths", "yank_count = %d, want cleared after paste",
                    out.yank_count);
    }
}

static void test_paste_pending_batch_works_from_select_mode_with_no_marks(void)
{
    Model in = make_nav_model(1, 0);
    in.mode = MODE_SELECT;
    strcpy(in.current_path, "/tmp");
    strcpy(in.entries[0].name, "placeholder");
    in.unfiltered_count = 0;

    strcpy(in.yank_paths[0], "/src/a.txt");
    strcpy(in.yank_paths[1], "/src/b.txt");
    in.yank_count = 2;
    in.yank_is_move = 1;

    Msg msg = { .type = MSG_PASTE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_MOVE || cmd.batch_count != 2) {
        TEST_ERRORF("paste from select mode replays batch yank", "cmd.type=%d batch_count=%d, want CMD_MOVE,2",
                    cmd.type, cmd.batch_count);
    }
    if (out.yank_count != 0) {
        TEST_ERRORF("paste from select mode replays batch yank", "yank_count = %d, want cleared", out.yank_count);
    }
}

static void test_yank_copy_on_file_inside_archive_records_archive_yank(void)
{
    ArchiveMember members[] = {
        make_archive_member("sub1", 1, 0),
        make_archive_member("sub1/notes.txt", 0, 12),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "sub1", "outer.tar",
                                         "/home/user/outer.tar/sub1");
    in.archive_stack[0].format = ARCHIVE_TAR;
    strcpy(in.archive_stack[0].source_path, "/home/user/outer.tar");
    strcpy(in.entries[0].name, "notes.txt");
    in.entries[0].st.st_mode = S_IFREG | 0644;
    in.entry_count = 1;
    in.selected = 0;

    Msg msg = { .type = MSG_YANK_COPY };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (!out.yank_from_archive) {
        TEST_ERRORF("yank copy on file inside archive", "yank_from_archive = 0, want 1");
    }
    if (out.yank_archive_format != ARCHIVE_TAR) {
        TEST_ERRORF("yank copy on file inside archive", "yank_archive_format = %d, want ARCHIVE_TAR",
                    out.yank_archive_format);
    }
    if (strcmp(out.yank_archive_source_path, "/home/user/outer.tar") != 0) {
        TEST_ERRORF("yank copy on file inside archive",
                    "yank_archive_source_path = '%s', want '/home/user/outer.tar'", out.yank_archive_source_path);
    }
    if (strcmp(out.yank_archive_member_path, "sub1/notes.txt") != 0) {
        TEST_ERRORF("yank copy on file inside archive", "yank_archive_member_path = '%s', want 'sub1/notes.txt'",
                    out.yank_archive_member_path);
    }
    if (out.yank_count != 0) {
        TEST_ERRORF("yank copy on file inside archive", "yank_count = %d, want 0 (empty sentinel)", out.yank_count);
    }
    if (out.mode != MODE_NAV) {
        TEST_ERRORF("yank copy on file inside archive", "mode = %d, want MODE_NAV", out.mode);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("yank copy on file inside archive", "cmd.type = %d, want CMD_NONE", cmd.type);
    }

    free(out.archive_stack[0].members);
}

static void test_yank_copy_on_directory_inside_archive_is_blocked(void)
{
    ArchiveMember members[] = {
        make_archive_member("sub1", 1, 0),
        make_archive_member("sub1/notes.txt", 0, 12),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "outer.tar",
                                         "/home/user/outer.tar");
    in.archive_stack[0].format = ARCHIVE_TAR;
    strcpy(in.archive_stack[0].source_path, "/home/user/outer.tar");
    strcpy(in.entries[0].name, "sub1");
    in.entries[0].st.st_mode = S_IFDIR | 0755;
    in.entry_count = 1;
    in.selected = 0;

    Msg msg = { .type = MSG_YANK_COPY };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_ERROR) {
        TEST_ERRORF("yank copy on directory inside archive is blocked", "mode = %d, want MODE_ERROR", out.mode);
    }
    if (strcmp(out.error_msg, "not possible in archive") != 0) {
        TEST_ERRORF("yank copy on directory inside archive is blocked",
                    "error_msg = '%s', want 'not possible in archive'", out.error_msg);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("yank copy on directory inside archive is blocked", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
    if (out.yank_from_archive) {
        TEST_ERRORF("yank copy on directory inside archive is blocked", "yank_from_archive = 1, want 0");
    }
    if (out.yank_count != 0) {
        TEST_ERRORF("yank copy on directory inside archive is blocked", "yank_count = %d, want 0", out.yank_count);
    }

    free(out.archive_stack[0].members);
}

static void test_paste_pending_from_archive_yank(void)
{
    typedef struct {
        const char *label;
        const char *member_path;
        const char *existing_unfiltered_entry;
        const char *expected_path3;
    } Case;

    Case cases[] = {
        {"paste from archive with no collision", "sub1/notes.txt", "other.txt", "/tmp/notes.txt"},
        {"paste from archive with name collision gets a numbered duplicate", "sub1/notes.txt", "notes.txt",
         "/tmp/notes (1).txt"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Model in = make_nav_model(1, 0);
        strcpy(in.current_path, "/tmp");
        strcpy(in.entries[0].name, "placeholder");
        strcpy(in.unfiltered_entries[0].name, cases[i].existing_unfiltered_entry);
        in.unfiltered_count = 1;

        in.yank_from_archive = 1;
        in.yank_archive_format = ARCHIVE_TAR;
        strcpy(in.yank_archive_source_path, "/home/user/outer.tar");
        strcpy(in.yank_archive_member_path, cases[i].member_path);

        Msg msg = { .type = MSG_PASTE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (cmd.type != CMD_EXTRACT_MEMBER_TO) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_EXTRACT_MEMBER_TO", cmd.type);
            continue;
        }
        if (cmd.archive_format != ARCHIVE_TAR) {
            TEST_ERRORF(cases[i].label, "cmd.archive_format = %d, want ARCHIVE_TAR", cmd.archive_format);
        }
        if (strcmp(cmd.path, "/home/user/outer.tar") != 0) {
            TEST_ERRORF(cases[i].label, "cmd.path = '%s', want '/home/user/outer.tar'", cmd.path);
        }
        if (strcmp(cmd.path2, cases[i].member_path) != 0) {
            TEST_ERRORF(cases[i].label, "cmd.path2 = '%s', want '%s'", cmd.path2, cases[i].member_path);
        }
        if (strcmp(cmd.path3, cases[i].expected_path3) != 0) {
            TEST_ERRORF(cases[i].label, "cmd.path3 = '%s', want '%s'", cmd.path3, cases[i].expected_path3);
        }
        if (out.yank_from_archive) {
            TEST_ERRORF(cases[i].label, "yank_from_archive = 1, want cleared to 0");
        }
        if (out.yank_archive_source_path[0] != '\0' || out.yank_archive_member_path[0] != '\0') {
            TEST_ERRORF(cases[i].label, "archive yank state not cleared after paste");
        }
    }
}

static void test_rename_inside_archive_is_blocked(void)
{
    ArchiveMember members[] = {
        make_archive_member("notes.txt", 0, 12),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "outer.tar",
                                         "/home/user/outer.tar");
    strcpy(in.entries[0].name, "notes.txt");
    in.entries[0].st.st_mode = S_IFREG | 0644;
    in.entry_count = 1;
    in.selected = 0;

    Msg msg = { .type = MSG_RENAME };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_ERROR) {
        TEST_ERRORF("rename inside archive is blocked", "mode = %d, want MODE_ERROR", out.mode);
    }
    if (strcmp(out.error_msg, "not possible in archive") != 0) {
        TEST_ERRORF("rename inside archive is blocked",
                    "error_msg = '%s', want 'not possible in archive'", out.error_msg);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("rename inside archive is blocked", "cmd.type = %d, want CMD_NONE", cmd.type);
    }

    free(out.archive_stack[0].members);
}

static void test_new_inside_archive_is_blocked(void)
{
    ArchiveMember members[] = {
        make_archive_member("notes.txt", 0, 12),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "outer.tar",
                                         "/home/user/outer.tar");
    strcpy(in.entries[0].name, "notes.txt");
    in.entries[0].st.st_mode = S_IFREG | 0644;
    in.entry_count = 1;
    in.selected = 0;

    Msg msg = { .type = MSG_NEW };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_ERROR) {
        TEST_ERRORF("new inside archive is blocked", "mode = %d, want MODE_ERROR", out.mode);
    }
    if (strcmp(out.error_msg, "not possible in archive") != 0) {
        TEST_ERRORF("new inside archive is blocked",
                    "error_msg = '%s', want 'not possible in archive'", out.error_msg);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("new inside archive is blocked", "cmd.type = %d, want CMD_NONE", cmd.type);
    }

    free(out.archive_stack[0].members);
}

static void test_run_cmd_inside_archive_is_blocked(void)
{
    ArchiveMember members[] = {
        make_archive_member("notes.txt", 0, 12),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "outer.tar",
                                         "/home/user/outer.tar");
    strcpy(in.entries[0].name, "notes.txt");
    in.entries[0].st.st_mode = S_IFREG | 0644;
    in.entry_count = 1;
    in.selected = 0;

    Msg msg = { .type = MSG_RUN_CMD };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_ERROR) {
        TEST_ERRORF("run cmd inside archive is blocked", "mode = %d, want MODE_ERROR", out.mode);
    }
    if (strcmp(out.error_msg, "not possible in archive") != 0) {
        TEST_ERRORF("run cmd inside archive is blocked",
                    "error_msg = '%s', want 'not possible in archive'", out.error_msg);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("run cmd inside archive is blocked", "cmd.type = %d, want CMD_NONE", cmd.type);
    }

    free(out.archive_stack[0].members);
}

static void test_delete_and_delete_permanent_inside_archive_are_blocked(void)
{
    MsgType types[] = { MSG_DELETE, MSG_DELETE_PERMANENT };

    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        ArchiveMember members[] = {
            make_archive_member("notes.txt", 0, 12),
        };
        int member_count = sizeof(members) / sizeof(members[0]);

        Model in = make_archive_level_model(members, member_count, "", "outer.tar",
                                             "/home/user/outer.tar");
        strcpy(in.entries[0].name, "notes.txt");
        in.entries[0].st.st_mode = S_IFREG | 0644;
        in.entry_count = 1;
        in.selected = 0;

        Msg msg = { .type = types[i] };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != MODE_ERROR) {
            TEST_ERRORF("delete inside archive is blocked", "mode = %d, want MODE_ERROR", out.mode);
        }
        if (strcmp(out.error_msg, "not possible in archive") != 0) {
            TEST_ERRORF("delete inside archive is blocked",
                        "error_msg = '%s', want 'not possible in archive'", out.error_msg);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF("delete inside archive is blocked", "cmd.type = %d, want CMD_NONE", cmd.type);
        }

        free(out.archive_stack[0].members);
    }
}

static void test_yank_move_inside_archive_is_blocked(void)
{
    ArchiveMember members[] = {
        make_archive_member("notes.txt", 0, 12),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "outer.tar",
                                         "/home/user/outer.tar");
    strcpy(in.entries[0].name, "notes.txt");
    in.entries[0].st.st_mode = S_IFREG | 0644;
    in.entry_count = 1;
    in.selected = 0;

    Msg msg = { .type = MSG_YANK_MOVE };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (out.mode != MODE_ERROR) {
        TEST_ERRORF("yank move inside archive is blocked", "mode = %d, want MODE_ERROR", out.mode);
    }
    if (strcmp(out.error_msg, "not possible in archive") != 0) {
        TEST_ERRORF("yank move inside archive is blocked",
                    "error_msg = '%s', want 'not possible in archive'", out.error_msg);
    }
    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("yank move inside archive is blocked", "cmd.type = %d, want CMD_NONE", cmd.type);
    }
    if (out.yank_count != 0) {
        TEST_ERRORF("yank move inside archive is blocked", "yank_count = %d, want 0", out.yank_count);
    }
    if (out.yank_is_move) {
        TEST_ERRORF("yank move inside archive is blocked", "yank_is_move = 1, want 0");
    }

    free(out.archive_stack[0].members);
}

static void test_paste_inside_archive_is_blocked_regardless_of_pending_yank(void)
{
    typedef struct {
        const char *label;
        int yank_from_archive;
        int yank_count;
        const char *yank_path0;
    } Case;

    Case cases[] = {
        {"paste into archive blocked with real yank_path pending", 0, 1, "/tmp/file.txt"},
        {"paste into archive blocked with archive-sourced yank pending", 1, 0, ""},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ArchiveMember members[] = {
            make_archive_member("notes.txt", 0, 12),
        };
        int member_count = sizeof(members) / sizeof(members[0]);

        Model in = make_archive_level_model(members, member_count, "", "outer.tar",
                                             "/home/user/outer.tar");
        strcpy(in.entries[0].name, "notes.txt");
        in.entries[0].st.st_mode = S_IFREG | 0644;
        in.entry_count = 1;
        in.selected = 0;

        in.yank_from_archive = cases[i].yank_from_archive;
        if (cases[i].yank_count > 0) {
            strcpy(in.yank_paths[0], cases[i].yank_path0);
            in.yank_count = cases[i].yank_count;
        }
        if (cases[i].yank_from_archive) {
            in.yank_archive_format = ARCHIVE_TAR;
            strcpy(in.yank_archive_source_path, "/home/user/other.tar");
            strcpy(in.yank_archive_member_path, "docs/readme.txt");
        }

        Msg msg = { .type = MSG_PASTE };
        Model out;
        Cmd cmd;

        update(&msg, &in, &out, &cmd);

        if (out.mode != MODE_ERROR) {
            TEST_ERRORF(cases[i].label, "mode = %d, want MODE_ERROR", out.mode);
        }
        if (strcmp(out.error_msg, "not possible in archive") != 0) {
            TEST_ERRORF(cases[i].label, "error_msg = '%s', want 'not possible in archive'", out.error_msg);
        }
        if (cmd.type != CMD_NONE) {
            TEST_ERRORF(cases[i].label, "cmd.type = %d, want CMD_NONE", cmd.type);
        }
        if (out.yank_from_archive != cases[i].yank_from_archive) {
            TEST_ERRORF(cases[i].label, "yank_from_archive = %d, want unchanged %d",
                        out.yank_from_archive, cases[i].yank_from_archive);
        }
        if (out.yank_count != cases[i].yank_count) {
            TEST_ERRORF(cases[i].label, "yank_count = %d, want unchanged %d", out.yank_count, cases[i].yank_count);
        }
        if (cases[i].yank_count > 0 && strcmp(out.yank_paths[0], cases[i].yank_path0) != 0) {
            TEST_ERRORF(cases[i].label, "yank_paths[0] = '%s', want unchanged '%s'",
                        out.yank_paths[0], cases[i].yank_path0);
        }
        if (cases[i].yank_from_archive) {
            if (strcmp(out.yank_archive_source_path, "/home/user/other.tar") != 0 ||
                strcmp(out.yank_archive_member_path, "docs/readme.txt") != 0) {
                TEST_ERRORF(cases[i].label, "archive yank state was cleared, want left intact");
            }
        }

        free(out.archive_stack[0].members);
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

static void test_cycle_sort_on_archive_sourced_entries_reorders_in_place(void)
{
    ArchiveMember members[] = {
        make_archive_member("zeta.txt", 0, 6),
        make_archive_member("alpha.txt", 0, 3),
        make_archive_member("src", 1, 0),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "project.zip",
                                         "/home/user/project.zip");
    in.entry_count = 3;
    strcpy(in.entries[0].name, "zeta.txt");
    in.entries[0].st.st_mode = S_IFREG;
    strcpy(in.entries[1].name, "alpha.txt");
    in.entries[1].st.st_mode = S_IFREG;
    strcpy(in.entries[2].name, "src");
    in.entries[2].st.st_mode = S_IFDIR;
    in.selected = 0;

    Msg msg = { .type = MSG_CYCLE_SORT };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("cycle sort on archive entries", "cmd.type = %d, want CMD_NONE (pure in-memory resort)", cmd.type);
    }
    if (out.sort_mode != SORT_NAME_DESC) {
        TEST_ERRORF("cycle sort on archive entries", "sort_mode = %d, want SORT_NAME_DESC", out.sort_mode);
    }
    if (out.entry_count != 3 || strcmp(out.entries[0].name, "src") != 0 ||
        strcmp(out.entries[1].name, "zeta.txt") != 0 || strcmp(out.entries[2].name, "alpha.txt") != 0) {
        TEST_ERRORF("cycle sort on archive entries",
                    "entries = [%s, %s, %s] (%d), want [src, zeta.txt, alpha.txt] (3) (dirs-first, name-desc)",
                    out.entries[0].name, out.entries[1].name, out.entries[2].name, out.entry_count);
    }

    free(out.archive_stack[0].members);
}

static void test_cycle_group_on_archive_sourced_entries_regroups_in_place(void)
{
    ArchiveMember members[] = {
        make_archive_member("alpha.txt", 0, 3),
        make_archive_member("src", 1, 0),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "project.zip",
                                         "/home/user/project.zip");
    in.entry_count = 2;
    strcpy(in.entries[0].name, "src");
    in.entries[0].st.st_mode = S_IFDIR;
    strcpy(in.entries[1].name, "alpha.txt");
    in.entries[1].st.st_mode = S_IFREG;
    in.selected = 0;

    Msg msg = { .type = MSG_CYCLE_GROUP };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("cycle group on archive entries", "cmd.type = %d, want CMD_NONE (pure in-memory regroup)", cmd.type);
    }
    if (out.group_mode != GROUP_DIRS_LAST) {
        TEST_ERRORF("cycle group on archive entries", "group_mode = %d, want GROUP_DIRS_LAST", out.group_mode);
    }
    if (out.entry_count != 2 || strcmp(out.entries[0].name, "alpha.txt") != 0 ||
        strcmp(out.entries[1].name, "src") != 0) {
        TEST_ERRORF("cycle group on archive entries",
                    "entries = [%s, %s] (%d), want [alpha.txt, src] (2) (dirs-last)",
                    out.entries[0].name, out.entries[1].name, out.entry_count);
    }

    free(out.archive_stack[0].members);
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

static void test_toggle_hidden_inside_archive_repopulates_entries_without_cmd(void)
{
    ArchiveMember members[] = {
        make_archive_member(".hidden.txt", 0, 3),
        make_archive_member("visible.txt", 0, 5),
    };
    int member_count = sizeof(members) / sizeof(members[0]);

    Model in = make_archive_level_model(members, member_count, "", "project.zip",
                                         "/home/user/project.zip");
    in.show_hidden = 0;
    in.entry_count = 1;
    strcpy(in.entries[0].name, "visible.txt");
    in.entries[0].st.st_mode = S_IFREG;

    Msg msg = { .type = MSG_TOGGLE_HIDDEN };
    Model out;
    Cmd cmd;

    update(&msg, &in, &out, &cmd);

    if (cmd.type != CMD_NONE) {
        TEST_ERRORF("toggle hidden inside archive", "cmd.type = %d, want CMD_NONE (no real-filesystem reload)", cmd.type);
    }
    if (!out.show_hidden) {
        TEST_ERRORF("toggle hidden inside archive", "show_hidden = %d, want 1", out.show_hidden);
    }
    if (out.entry_count != 2) {
        TEST_ERRORF("toggle hidden inside archive", "entry_count = %d, want 2", out.entry_count);
    } else {
        int found_hidden = 0, found_visible = 0;
        for (int i = 0; i < out.entry_count; i++) {
            if (strcmp(out.entries[i].name, ".hidden.txt") == 0)
                found_hidden = 1;
            if (strcmp(out.entries[i].name, "visible.txt") == 0)
                found_visible = 1;
        }
        if (!found_hidden || !found_visible) {
            TEST_ERRORF("toggle hidden inside archive",
                        "entries missing expected names (found_hidden=%d, found_visible=%d)",
                        found_hidden, found_visible);
        }
    }

    Model in2 = out;
    Model out2;
    Cmd cmd2;
    update(&msg, &in2, &out2, &cmd2);

    if (cmd2.type != CMD_NONE) {
        TEST_ERRORF("toggle hidden inside archive back off", "cmd.type = %d, want CMD_NONE", cmd2.type);
    }
    if (out2.show_hidden) {
        TEST_ERRORF("toggle hidden inside archive back off", "show_hidden = %d, want 0", out2.show_hidden);
    }
    if (out2.entry_count != 1 || strcmp(out2.entries[0].name, "visible.txt") != 0) {
        TEST_ERRORF("toggle hidden inside archive back off",
                    "entries = [%s] (%d), want [visible.txt] (1)",
                    out2.entries[0].name, out2.entry_count);
    }

    free(out2.archive_stack[0].members);
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
    test_activate_on_archive_file_lists_archive_instead_of_launching_editor();
    test_activate_on_archive_file_resets_active_filter_and_glob();
    test_activate_on_non_archive_file_still_launches_editor();
    test_preview();
    test_archive_listed_pushes_first_level_and_populates_root_entries();
    test_archive_listed_populates_entries_without_fabricated_permission_bits();
    test_archive_listed_beyond_128_members_is_not_truncated();
    test_archive_listed_at_max_depth_is_a_noop();
    test_activate_on_archive_directory_descends_subfolder_in_place();
    test_go_parent_inside_archive_pops_one_subfolder_segment();
    test_go_parent_at_archive_root_pops_level_to_real_filesystem();
    test_go_parent_at_nested_archive_root_pops_to_containing_level();
    test_activate_on_archive_member_inside_archive_extracts_member_first();
    test_activate_on_archive_member_at_max_depth_does_not_extract();
    test_activate_on_plain_file_inside_archive_opens_archive_member();
    test_preview_on_file_inside_archive_previews_archive_member();
    test_member_extracted_issues_list_archive_for_nested_format();
    test_archive_listed_from_tmp_source_pushes_nested_level();
    test_go_parent_at_nested_archive_root_deletes_tmp_source_file();
    test_filter_inside_archive_narrows_from_level_current_subfolder();
    test_dir_loaded();
    test_op_succeeded();
    test_op_succeeded_rebuilds_glob_when_active();
    test_op_succeeded_inside_archive_refreshes_entries_in_place();
    test_op_succeeded_inside_archive_with_glob_active_recomputes_glob_in_place();
    test_op_failed();
    test_start_edit();
    test_enter_filter_mode();
    test_enter_glob_mode();
    test_enter_glob_mode_entries();
    test_glob_filter_mutual_exclusion();
    test_enter_select_mode_from_nav();
    test_leave_select_mode();
    test_select_mode_rejected_while_glob_active();
    test_select_mode_allowed_while_filter_active();
    test_toggle_mark_marks_unmarked_entry();
    test_toggle_mark_unmarks_marked_entry();
    test_toggle_mark_all_marks_everything_when_not_all_marked();
    test_toggle_mark_all_clears_everything_when_all_marked();
    test_leave_select_mode_clears_marks();
    test_range_select_from_unmarked_anchor_marks_run_while_moving_down();
    test_range_select_from_marked_anchor_unmarks_run_while_moving_up();
    test_range_select_backtracking_over_swept_entry_does_not_revert();
    test_space_toggles_independently_while_range_active();
    test_second_range_select_press_stops_extend_and_keeps_marks();
    test_go_parent_while_select_mode_keeps_marks_and_mode();
    test_activate_into_subdir_while_select_mode_keeps_marks_and_mode();
    test_marking_in_new_directory_discards_old_marks();
    test_range_select_in_new_directory_discards_old_marks();
    test_mark_all_in_new_directory_discards_old_marks();
    test_sort_group_filter_glob_are_noop_in_select_mode_with_marks();
    test_sort_works_in_select_mode_without_marks();
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
    test_glob_commit_inside_archive_populates_entries_directly();
    test_glob_commit_inside_archive_caps_and_reports_truncation();
    test_glob_commit_inside_archive_subfolder_uses_relative_names();
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
    test_recall_prev_first_press_recalls_most_recent_and_stashes_draft();
    test_recall_prev_repeated_walks_older_and_clamps_at_oldest();
    test_recall_next_walks_toward_newest();
    test_recall_next_past_newest_restores_stash_and_resets();
    test_recall_prev_and_next_on_zero_history_folder_is_noop();
    test_recall_is_scoped_to_mode_run_cmd();
    test_validate_rename();
    test_rename_clears_yank_when_source_matches();
    test_rename_leaves_yank_for_different_entry();
    test_delete_requests_confirmation();
    test_confirm_delete_yes_deletes();
    test_confirm_delete_clears_yank_when_target_matches();
    test_confirm_delete_leaves_yank_for_different_entry();
    test_confirm_delete_anything_else_cancels();
    test_batch_delete_with_marks_triggers_combined_confirm();
    test_delete_in_select_mode_with_zero_marks_targets_cursor();
    test_delete_outside_select_mode_ignores_stray_marks();
    test_confirm_batch_delete_yes_builds_batch_cmd_and_clears_marks();
    test_confirm_batch_delete_no_leaves_marks_untouched();
    test_confirm_batch_delete_targets_marks_directory_not_currently_displayed_dir();
    test_batch_yank_copy_and_move_capture_all_marked_paths();
    test_yank_in_select_mode_with_zero_marks_targets_cursor();
    test_yank_outside_select_mode_ignores_stray_marks();
    test_batch_yank_targets_marks_directory_not_currently_displayed_dir();
    test_yank();
    test_yank_replaces_pending();
    test_nav_cancel_clears_pending_yank();
    test_nav_cancel_with_no_pending_yank_is_noop();
    test_paste_nothing_pending_is_noop();
    test_paste_pending();
    test_paste_pending_batch_replays_all_yanked_paths();
    test_paste_pending_batch_works_from_select_mode_with_no_marks();
    test_yank_copy_on_file_inside_archive_records_archive_yank();
    test_yank_copy_on_directory_inside_archive_is_blocked();
    test_paste_pending_from_archive_yank();
    test_rename_inside_archive_is_blocked();
    test_new_inside_archive_is_blocked();
    test_run_cmd_inside_archive_is_blocked();
    test_delete_and_delete_permanent_inside_archive_are_blocked();
    test_yank_move_inside_archive_is_blocked();
    test_paste_inside_archive_is_blocked_regardless_of_pending_yank();
    test_cycle_page_advances_to_next_page();
    test_cycle_page_wraps_from_last_page();
    test_cycle_page_noop_when_everything_fits();
    test_cycle_sort_wraps_through_all_eight_states();
    test_cycle_group_wraps_through_all_three_states();
    test_cycle_sort_on_archive_sourced_entries_reorders_in_place();
    test_cycle_group_on_archive_sourced_entries_regroups_in_place();
    test_toggle_hidden();
    test_toggle_hidden_inside_archive_repopulates_entries_without_cmd();
    test_resort_keeps_selection_on_same_file();
    test_dir_loaded_sorts_entries();
    test_dir_loaded_reapplies_committed_filter();
    test_quit();
    test_error_dismissed_by_any_key();
}

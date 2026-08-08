#include "model.h"
#include "msg.h"
#include "cmd.h"
#include "update.h"
#include "view.h"
#include "helpers.h"
#include "../vendor/termbox2.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define PREVIEW_SNIFF_LEN 512

/* main() is the only impure code in the program: the only place that calls
 * a tb_* function, and the only place that touches the filesystem or spawns
 * a process. Everything it decides is delegated to the pure update()/view()
 * core; everything it does is one Cmd at a time, translated from update()'s
 * output. */

static Msg msg_failed(const char *fmt, ...)
{
    Msg msg = { .type = MSG_OP_FAILED };

    va_list args;
    va_start(args, fmt);
    vsnprintf(msg.error, sizeof(msg.error), fmt, args);
    va_end(args);

    return msg;
}

static Msg execute_load_dir(const char *path)
{
    static Entry loaded[MAX_ENTRIES];

    DIR *dir = opendir(path);
    if (!dir)
        return msg_failed("%s: %s", path, strerror(errno));

    int count = 0;
    struct dirent *de;
    char fullpath[PATH_MAX_LEN];

    while ((de = readdir(dir)) && count < MAX_ENTRIES) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, de->d_name);
        if (lstat(fullpath, &loaded[count].st) == 0) {
            strncpy(loaded[count].name, de->d_name, NAME_MAX_LEN);
            loaded[count].name[NAME_MAX_LEN] = '\0';
            count++;
        }
    }
    closedir(dir);

    Msg msg = { .type = MSG_DIR_LOADED };
    msg.dir_loaded.entries = loaded;
    msg.dir_loaded.entry_count = count;
    strncpy(msg.dir_loaded.path, path, sizeof(msg.dir_loaded.path) - 1);
    msg.dir_loaded.path[sizeof(msg.dir_loaded.path) - 1] = '\0';
    return msg;
}

static Msg execute_rename(const char *from, const char *to)
{
    if (rename(from, to) != 0)
        return msg_failed("rename: %s", strerror(errno));
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_create_file(const char *path)
{
    FILE *f = fopen(path, "wx");
    if (!f)
        return msg_failed("create file: %s", strerror(errno));
    fclose(f);
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_create_dir(const char *path)
{
    if (mkdir(path, 0755) != 0)
        return msg_failed("create directory: %s", strerror(errno));
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_delete(const char *path, int is_dir)
{
    int rc = is_dir ? rmdir(path) : unlink(path);
    if (rc != 0)
        return msg_failed("delete: %s", strerror(errno));
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_launch_editor(const char *path)
{
    tb_shutdown();

    pid_t pid = fork();
    if (pid == 0) {
        execlp("vim", "vim", path, (char *)NULL);
        _exit(EXIT_FAILURE);
    } else {
        wait(NULL);
    }

    tb_init();
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_preview(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return msg_failed("preview: %s", strerror(errno));

    unsigned char sniff[PREVIEW_SNIFF_LEN];
    size_t n = fread(sniff, 1, sizeof(sniff), f);
    fclose(f);

    if (is_binary_content(sniff, n))
        return msg_failed("preview: binary file");

    tb_shutdown();

    pid_t pid = fork();
    if (pid == 0) {
        execlp("more", "more", path, (char *)NULL);
        _exit(EXIT_FAILURE);
    } else {
        wait(NULL);
    }

    tb_init();
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_cmd(const Cmd *cmd)
{
    switch (cmd->type) {
    case CMD_LOAD_DIR:      return execute_load_dir(cmd->path);
    case CMD_RENAME:        return execute_rename(cmd->path, cmd->path2);
    case CMD_CREATE_FILE:   return execute_create_file(cmd->path);
    case CMD_CREATE_DIR:    return execute_create_dir(cmd->path);
    case CMD_DELETE:        return execute_delete(cmd->path, cmd->is_dir);
    case CMD_LAUNCH_EDITOR: return execute_launch_editor(cmd->path);
    case CMD_PREVIEW:       return execute_preview(cmd->path);
    default:                return (Msg){ .type = MSG_NONE };
    }
}

static void style_colors(StyleTag style, uintattr_t *fg, uintattr_t *bg)
{
    switch (style) {
    case STYLE_SELECTED:
        /* Explicit colors instead of bare TB_REVERSE: some terminals don't
         * swap TB_DEFAULT/TB_DEFAULT visibly, which renders as no contrast
         * at all rather than a highlighted row. */
        *fg = TB_BLACK;
        *bg = TB_WHITE;
        break;
    default:
        *fg = TB_DEFAULT;
        *bg = TB_DEFAULT;
        break;
    }
}

static void render(const Model *model)
{
    View v = view(model);

    tb_clear();
    for (int i = 0; i < v.line_count; i++) {
        uintattr_t fg, bg;
        style_colors(v.lines[i].style, &fg, &bg);

        int row = (i == v.line_count - 1) ? tb_height() - 1 : i;
        tb_print(0, row, fg, bg, v.lines[i].text);
    }
    tb_present();
}

/* Command keys (MSG_RENAME/MSG_NEW_FILE/MSG_NEW_DIR/MSG_QUIT) are only
 * recognized outside text-entry modes, so typing "r" while naming a file
 * inserts the letter instead of re-triggering rename. */
static Msg translate_event(struct tb_event ev, AppMode mode)
{
    Msg msg = { .type = MSG_NONE };

    if (ev.type != TB_EVENT_KEY)
        return msg;

    int text_entry = (mode == MODE_RENAME || mode == MODE_CREATE_FILE || mode == MODE_CREATE_DIR);

    if (text_entry) {
        if (ev.key == TB_KEY_ESC)
            msg.type = MSG_CANCEL;
        else if (ev.key == TB_KEY_ENTER)
            msg.type = MSG_ACTIVATE;
        else if (ev.key == TB_KEY_BACKSPACE || ev.key == TB_KEY_BACKSPACE2 || ev.key == TB_KEY_DELETE)
            msg.type = MSG_DELETE;
        else if (ev.ch != 0 && isprint((int)ev.ch)) {
            msg.type = MSG_TEXT_INPUT;
            msg.ch = (char)ev.ch;
        }
        return msg;
    }

    switch (ev.key) {
    case TB_KEY_ARROW_UP:
        msg.type = MSG_MOVE_UP;
        return msg;
    case TB_KEY_ARROW_DOWN:
        msg.type = MSG_MOVE_DOWN;
        return msg;
    case TB_KEY_ARROW_LEFT:
        msg.type = MSG_GO_PARENT;
        return msg;
    case TB_KEY_ARROW_RIGHT:
    case TB_KEY_ENTER:
        msg.type = MSG_ACTIVATE;
        return msg;
    case TB_KEY_BACKSPACE:
    case TB_KEY_BACKSPACE2:
    case TB_KEY_DELETE:
        msg.type = MSG_DELETE;
        return msg;
    case TB_KEY_ESC:
        msg.type = MSG_CANCEL;
        return msg;
    default:
        break;
    }

    if (ev.ch == 'r' || ev.ch == 'R')
        msg.type = MSG_RENAME;
    else if (ev.ch == 'f')
        msg.type = MSG_NEW_FILE;
    else if (ev.ch == 'd')
        msg.type = MSG_NEW_DIR;
    else if (ev.ch == ' ')
        msg.type = MSG_PREVIEW;
    else if (ev.ch == 'q')
        msg.type = MSG_QUIT;
    else if (ev.ch != 0) {
        msg.type = MSG_TEXT_INPUT;
        msg.ch = (char)ev.ch;
    }

    return msg;
}

int main(void)
{
    tb_init();

    Model model;
    memset(&model, 0, sizeof(model));
    model.mode = MODE_NAV;
    getcwd(model.current_path, sizeof(model.current_path));

    Cmd cmd = { .type = CMD_LOAD_DIR };
    strncpy(cmd.path, model.current_path, sizeof(cmd.path) - 1);

    while (!model.should_quit) {
        if (cmd.type != CMD_NONE) {
            Msg outcome = execute_cmd(&cmd);
            Model next_model;
            Cmd next_cmd;
            update(&outcome, &model, &next_model, &next_cmd);
            model = next_model;
            cmd = next_cmd;
            continue;
        }

        render(&model);

        struct tb_event ev;
        tb_poll_event(&ev);
        Msg msg = translate_event(ev, model.mode);

        Model next_model;
        Cmd next_cmd;
        update(&msg, &model, &next_model, &next_cmd);
        model = next_model;
        cmd = next_cmd;
    }

    tb_shutdown();
    return EXIT_SUCCESS;
}

#include "update.h"
#include "helpers.h"
#include <stdio.h>
#include <string.h>

static void join_path(const char *dir, const char *name, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/%s", dir, name);
}

static void parent_path(const char *path, char *out, size_t out_size)
{
    strncpy(out, path, out_size - 1);
    out[out_size - 1] = '\0';

    if (strcmp(out, "/") == 0)
        return;

    char *slash = strrchr(out, '/');
    if (!slash)
        return;

    if (slash == out)
        out[1] = '\0';
    else
        *slash = '\0';
}

static void start_edit(Model *out_model, AppMode new_mode)
{
    out_model->mode = new_mode;
    out_model->edit_buf[0] = '\0';
    out_model->edit_len = 0;

    if (new_mode == MODE_CREATE)
        out_model->selected = out_model->entry_count;
}

static void cancel_edit(Model *out_model)
{
    out_model->mode = MODE_NAV;
    if (out_model->selected >= out_model->entry_count && out_model->entry_count > 0)
        out_model->selected = out_model->entry_count - 1;
}

static void handle_nav(const Msg *msg, Model *out_model, Cmd *out_cmd)
{
    switch (msg->type) {
    case MSG_RENAME:
        if (out_model->selected < out_model->entry_count &&
            !is_protected_name(out_model->entries[out_model->selected].name))
            start_edit(out_model, MODE_RENAME);
        break;

    case MSG_NEW:
        start_edit(out_model, MODE_CREATE);
        break;

    case MSG_DELETE:
        if (out_model->selected < out_model->entry_count &&
            !is_protected_name(out_model->entries[out_model->selected].name))
            out_model->mode = MODE_CONFIRM_DELETE;
        break;

    case MSG_YANK_COPY:
    case MSG_YANK_MOVE:
        if (out_model->selected < out_model->entry_count &&
            !is_protected_name(out_model->entries[out_model->selected].name)) {
            join_path(out_model->current_path, out_model->entries[out_model->selected].name,
                      out_model->yank_path, sizeof(out_model->yank_path));
            out_model->yank_is_move = (msg->type == MSG_YANK_MOVE);
        }
        break;

    case MSG_PASTE: {
        if (out_model->yank_path[0] == '\0')
            break;

        const char *slash = strrchr(out_model->yank_path, '/');
        const char *base_name = slash ? slash + 1 : out_model->yank_path;

        char resolved_name[NAME_MAX_LEN + 1];
        find_available_name(base_name, out_model->entries, out_model->entry_count,
                             resolved_name, sizeof(resolved_name));

        out_cmd->type = out_model->yank_is_move ? CMD_MOVE : CMD_COPY;
        strncpy(out_cmd->path, out_model->yank_path, sizeof(out_cmd->path) - 1);
        out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
        join_path(out_model->current_path, resolved_name, out_cmd->path2, sizeof(out_cmd->path2));

        out_model->yank_path[0] = '\0';
        break;
    }

    case MSG_QUIT:
        out_model->should_quit = 1;
        break;

    case MSG_MOVE_UP:
        if (out_model->selected > 0)
            out_model->selected--;
        break;

    case MSG_MOVE_DOWN:
        if (out_model->selected < out_model->entry_count - 1)
            out_model->selected++;
        break;

    case MSG_GO_PARENT:
        out_cmd->type = CMD_LOAD_DIR;
        parent_path(out_model->current_path, out_cmd->path, sizeof(out_cmd->path));
        break;

    case MSG_PREVIEW: {
        if (out_model->selected >= out_model->entry_count)
            break;

        Entry *e = &out_model->entries[out_model->selected];
        if (S_ISREG(e->st.st_mode)) {
            out_cmd->type = CMD_PREVIEW;
            join_path(out_model->current_path, e->name, out_cmd->path, sizeof(out_cmd->path));
        }
        break;
    }

    case MSG_ACTIVATE: {
        if (out_model->selected >= out_model->entry_count)
            break;

        Entry *e = &out_model->entries[out_model->selected];
        if (S_ISDIR(e->st.st_mode)) {
            if (is_protected_name(e->name))
                break;
            out_cmd->type = CMD_LOAD_DIR;
            join_path(out_model->current_path, e->name, out_cmd->path, sizeof(out_cmd->path));
        } else if (S_ISREG(e->st.st_mode)) {
            out_cmd->type = CMD_LAUNCH_EDITOR;
            join_path(out_model->current_path, e->name, out_cmd->path, sizeof(out_cmd->path));
        }
        break;
    }

    default:
        break;
    }
}

static void handle_edit(const Msg *msg, Model *out_model, Cmd *out_cmd)
{
    switch (msg->type) {
    case MSG_CANCEL:
        cancel_edit(out_model);
        break;

    case MSG_DELETE:
        if (out_model->edit_len > 0)
            out_model->edit_buf[--out_model->edit_len] = '\0';
        break;

    case MSG_TEXT_INPUT:
        if (out_model->edit_len < NAME_MAX_LEN) {
            out_model->edit_buf[out_model->edit_len++] = msg->ch;
            out_model->edit_buf[out_model->edit_len] = '\0';
        }
        break;

    case MSG_ACTIVATE:
        if (out_model->edit_len == 0) {
            cancel_edit(out_model);
            break;
        }

        if (out_model->mode == MODE_RENAME) {
            out_cmd->type = CMD_RENAME;
            join_path(out_model->current_path, out_model->entries[out_model->selected].name,
                      out_cmd->path, sizeof(out_cmd->path));
            join_path(out_model->current_path, out_model->edit_buf,
                      out_cmd->path2, sizeof(out_cmd->path2));
        } else if (out_model->mode == MODE_CREATE) {
            char name[NAME_MAX_LEN + 1];
            NameKind kind = classify_new_name(out_model->edit_buf, name, sizeof(name));
            if (kind == NAME_IS_DIR) {
                out_cmd->type = CMD_CREATE_DIR;
                join_path(out_model->current_path, name, out_cmd->path, sizeof(out_cmd->path));
            } else if (kind == NAME_IS_FILE) {
                out_cmd->type = CMD_CREATE_FILE;
                join_path(out_model->current_path, name, out_cmd->path, sizeof(out_cmd->path));
            }
        }

        cancel_edit(out_model);
        break;

    default:
        break;
    }
}

static void handle_confirm_delete(const Msg *msg, Model *out_model, Cmd *out_cmd)
{
    out_model->mode = MODE_NAV;

    if (msg->type != MSG_TEXT_INPUT || (msg->ch != 'y' && msg->ch != 'Y'))
        return;

    Entry *e = &out_model->entries[out_model->selected];
    out_cmd->type = CMD_DELETE;
    out_cmd->is_dir = S_ISDIR(e->st.st_mode);
    join_path(out_model->current_path, e->name, out_cmd->path, sizeof(out_cmd->path));
}

static void handle_dir_loaded(const Msg *msg, Model *out_model)
{
    out_model->entry_count = msg->dir_loaded.entry_count;
    memcpy(out_model->entries, msg->dir_loaded.entries,
           sizeof(Entry) * msg->dir_loaded.entry_count);
    strncpy(out_model->current_path, msg->dir_loaded.path,
            sizeof(out_model->current_path) - 1);
    out_model->current_path[sizeof(out_model->current_path) - 1] = '\0';

    if (out_model->selected >= out_model->entry_count)
        out_model->selected = out_model->entry_count > 0 ? out_model->entry_count - 1 : 0;
}

void update(const Msg *msg, const Model *model, Model *out_model, Cmd *out_cmd)
{
    *out_model = *model;
    out_cmd->type = CMD_NONE;

    switch (msg->type) {
    case MSG_DIR_LOADED:
        handle_dir_loaded(msg, out_model);
        return;

    case MSG_OP_SUCCEEDED:
        out_cmd->type = CMD_LOAD_DIR;
        strncpy(out_cmd->path, out_model->current_path, sizeof(out_cmd->path) - 1);
        out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
        return;

    case MSG_OP_FAILED:
        out_model->mode = MODE_ERROR;
        strncpy(out_model->error_msg, msg->error, sizeof(out_model->error_msg) - 1);
        out_model->error_msg[sizeof(out_model->error_msg) - 1] = '\0';
        return;

    default:
        break;
    }

    if (model->mode == MODE_NAV)
        handle_nav(msg, out_model, out_cmd);
    else if (model->mode == MODE_RENAME || model->mode == MODE_CREATE)
        handle_edit(msg, out_model, out_cmd);
    else if (model->mode == MODE_CONFIRM_DELETE)
        handle_confirm_delete(msg, out_model, out_cmd);
    else if (model->mode == MODE_ERROR)
        out_model->mode = MODE_NAV;
}

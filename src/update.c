#include "update.h"
#include "helpers.h"
#include <stdio.h>
#include <string.h>

static void join_path(const char *dir, const char *name, char *out, size_t out_size)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(out, out_size, "%s/%s", dir, name);
#pragma GCC diagnostic pop
}

static void parent_path(const char *path, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", path);

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

static void recompute_scroll(Model *out_model)
{
    int visible_rows = visible_entry_rows(out_model->term_height, model_has_virtual_line(out_model));
    out_model->scroll_offset = page_snap_offset(out_model->selected, out_model->entry_count, visible_rows);
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

static void selected_name(const Model *m, char *out, size_t out_size)
{
    if (m->selected < m->entry_count)
        strncpy(out, m->entries[m->selected].name, out_size - 1);
    else
        out[0] = '\0';
    out[out_size - 1] = '\0';
}

/* Sorts out_model->entries per its current sort_mode/group_mode, then
 * re-locates prev_name in the new order so the cursor stays on the same
 * file across a resort. Falls back to the usual entry_count clamp if
 * prev_name isn't found (not expected in normal operation). */
static void resort_and_relocate(Model *out_model, const char *prev_name)
{
    sort_entries(out_model->entries, out_model->entry_count, out_model->sort_mode, out_model->group_mode);

    out_model->selected = 0;
    if (prev_name && prev_name[0] != '\0') {
        for (int i = 0; i < out_model->entry_count; i++) {
            if (strcmp(out_model->entries[i].name, prev_name) == 0) {
                out_model->selected = i;
                break;
            }
        }
    }

    recompute_scroll(out_model);
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

    case MSG_RUN_CMD:
        start_edit(out_model, MODE_RUN_CMD);
        break;

    case MSG_DELETE:
    case MSG_DELETE_PERMANENT:
        if (out_model->selected < out_model->entry_count &&
            !is_protected_name(out_model->entries[out_model->selected].name)) {
            out_model->mode = MODE_CONFIRM_DELETE;
            out_model->confirm_permanent_delete = (msg->type == MSG_DELETE_PERMANENT);
        }
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

    case MSG_CYCLE_SORT: {
        char prev_name[NAME_MAX_LEN + 1];
        selected_name(out_model, prev_name, sizeof(prev_name));
        out_model->sort_mode = (out_model->sort_mode + 1) % SORT_MODE_COUNT;
        resort_and_relocate(out_model, prev_name);
        break;
    }

    case MSG_CYCLE_GROUP: {
        char prev_name[NAME_MAX_LEN + 1];
        selected_name(out_model, prev_name, sizeof(prev_name));
        out_model->group_mode = (out_model->group_mode + 1) % GROUP_MODE_COUNT;
        resort_and_relocate(out_model, prev_name);
        break;
    }

    case MSG_CYCLE_PAGE: {
        int visible_rows = visible_entry_rows(out_model->term_height, model_has_virtual_line(out_model));
        if (visible_rows > 0 && out_model->entry_count > visible_rows) {
            int next_start = out_model->scroll_offset + visible_rows;
            if (next_start >= out_model->entry_count)
                next_start = 0;
            out_model->selected = next_start;
            recompute_scroll(out_model);
        }
        break;
    }

    case MSG_TOGGLE_HIDDEN:
        out_model->show_hidden = !out_model->show_hidden;
        out_cmd->type = CMD_LOAD_DIR;
        out_cmd->show_hidden = out_model->show_hidden;
        strncpy(out_cmd->path, out_model->current_path, sizeof(out_cmd->path) - 1);
        out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
        break;

    case MSG_QUIT:
        out_model->should_quit = 1;
        break;

    case MSG_MOVE_UP:
        if (out_model->selected > 0)
            out_model->selected--;
        recompute_scroll(out_model);
        break;

    case MSG_MOVE_DOWN:
        if (out_model->selected < out_model->entry_count - 1)
            out_model->selected++;
        recompute_scroll(out_model);
        break;

    case MSG_GO_PARENT:
        out_cmd->type = CMD_LOAD_DIR;
        out_cmd->show_hidden = out_model->show_hidden;
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
            out_cmd->show_hidden = out_model->show_hidden;
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
        } else if (out_model->mode == MODE_RUN_CMD) {
            if (out_model->edit_buf[0] == '!' && out_model->edit_buf[1] != '\0') {
                out_cmd->type = CMD_RUN;
                strncpy(out_cmd->path, out_model->current_path, sizeof(out_cmd->path) - 1);
                out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
                strncpy(out_cmd->cmd_text, out_model->edit_buf + 1, sizeof(out_cmd->cmd_text) - 1);
                out_cmd->cmd_text[sizeof(out_cmd->cmd_text) - 1] = '\0';
                if (out_model->selected < out_model->entry_count)
                    join_path(out_model->current_path, out_model->entries[out_model->selected].name,
                              out_cmd->selected_path, sizeof(out_cmd->selected_path));
                else
                    out_cmd->selected_path[0] = '\0';
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
    out_cmd->type = out_model->confirm_permanent_delete ? CMD_DELETE : CMD_TRASH;
    out_cmd->is_dir = S_ISDIR(e->st.st_mode);
    join_path(out_model->current_path, e->name, out_cmd->path, sizeof(out_cmd->path));
}

static void handle_dir_loaded(const Msg *msg, Model *out_model)
{
    char prev_name[NAME_MAX_LEN + 1];
    selected_name(out_model, prev_name, sizeof(prev_name));

    out_model->entry_count = msg->dir_loaded.entry_count;
    memcpy(out_model->entries, msg->dir_loaded.entries,
           sizeof(Entry) * msg->dir_loaded.entry_count);
    strncpy(out_model->current_path, msg->dir_loaded.path,
            sizeof(out_model->current_path) - 1);
    out_model->current_path[sizeof(out_model->current_path) - 1] = '\0';

    resort_and_relocate(out_model, prev_name);
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
        out_cmd->show_hidden = out_model->show_hidden;
        strncpy(out_cmd->path, out_model->current_path, sizeof(out_cmd->path) - 1);
        out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
        return;

    case MSG_OP_FAILED:
        out_model->mode = MODE_ERROR;
        strncpy(out_model->error_msg, msg->error, sizeof(out_model->error_msg) - 1);
        out_model->error_msg[sizeof(out_model->error_msg) - 1] = '\0';
        return;

    case MSG_RESIZE:
        out_model->term_height = msg->resize.height;
        out_model->term_width = msg->resize.width;
        recompute_scroll(out_model);
        return;

    default:
        break;
    }

    if (model->mode == MODE_NAV)
        handle_nav(msg, out_model, out_cmd);
    else if (model->mode == MODE_RENAME || model->mode == MODE_CREATE || model->mode == MODE_RUN_CMD)
        handle_edit(msg, out_model, out_cmd);
    else if (model->mode == MODE_CONFIRM_DELETE)
        handle_confirm_delete(msg, out_model, out_cmd);
    else if (model->mode == MODE_ERROR)
        out_model->mode = MODE_NAV;
}

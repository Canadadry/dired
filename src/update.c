#include "update.h"
#include "helpers.h"
#include "archive.h"
#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void append_subfolder_segment(const char *subfolder, const char *name, char *out, size_t out_size)
{
    if (subfolder[0] == '\0')
        snprintf(out, out_size, "%s", name);
    else
        snprintf(out, out_size, "%s/%s", subfolder, name);
}

static void pop_subfolder_segment(char *subfolder)
{
    char *slash = strrchr(subfolder, '/');
    if (slash)
        *slash = '\0';
    else
        subfolder[0] = '\0';
}

static void basename_of(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    strncpy(out, name, out_size - 1);
    out[out_size - 1] = '\0';
}

static void recompute_scroll(Model *out_model)
{
    int visible_rows = visible_entry_rows(out_model->term_height, model_has_virtual_line(out_model));
    out_model->scroll_offset = page_snap_offset(out_model->selected, out_model->entry_count, visible_rows);
}

static void move_cursor(Model *out_model, int delta)
{
    int max_index = out_model->entry_count > 0 ? out_model->entry_count - 1 : 0;
    int next = out_model->selected + delta;

    if (next < 0)
        next = 0;
    else if (next > max_index)
        next = max_index;

    out_model->selected = next;
    recompute_scroll(out_model);
}

static void set_mark(Model *out_model, int idx, int value)
{
    if (idx < 0 || idx >= out_model->entry_count)
        return;
    if (out_model->marked[idx] == (unsigned char)value)
        return;

    out_model->marked[idx] = (unsigned char)value;

    if (value) {
        char path[PATH_MAX_LEN];
        join_path(out_model->current_path, out_model->entries[idx].name, path, sizeof(path));

        MarkedItem *item = &out_model->marked_items[out_model->marked_count];
        snprintf(item->path, sizeof(item->path), "%s", path);
        item->is_dir = S_ISDIR(out_model->entries[idx].st.st_mode);
        out_model->marked_count++;
    } else {
        char path[PATH_MAX_LEN];
        join_path(out_model->current_path, out_model->entries[idx].name, path, sizeof(path));
        for (int i = 0; i < out_model->marked_count; i++) {
            if (strcmp(out_model->marked_items[i].path, path) == 0) {
                out_model->marked_items[i] = out_model->marked_items[out_model->marked_count - 1];
                break;
            }
        }
        out_model->marked_count--;
    }
}

static void start_edit(Model *out_model, AppMode new_mode)
{
    out_model->mode = new_mode;
    out_model->edit_buf[0] = '\0';
    out_model->edit_len = 0;
    out_model->recall_cursor = 0;

    if (new_mode == MODE_CREATE)
        out_model->selected = out_model->entry_count;
}

static void cancel_edit(Model *out_model)
{
    out_model->mode = MODE_NAV;
    if (out_model->selected >= out_model->entry_count && out_model->entry_count > 0)
        out_model->selected = out_model->entry_count - 1;
}

static void block_in_archive(Model *out_model)
{
    out_model->mode = MODE_ERROR;
    strncpy(out_model->error_msg, "not possible in archive", sizeof(out_model->error_msg) - 1);
    out_model->error_msg[sizeof(out_model->error_msg) - 1] = '\0';
}

static void block_select_on_glob(Model *out_model)
{
    out_model->mode = MODE_ERROR;
    strncpy(out_model->error_msg, "selection mode not available on glob results (spans multiple directories)",
            sizeof(out_model->error_msg) - 1);
    out_model->error_msg[sizeof(out_model->error_msg) - 1] = '\0';
}

static void selected_name(const Model *m, char *out, size_t out_size)
{
    if (m->selected < m->entry_count)
        strncpy(out, m->entries[m->selected].name, out_size - 1);
    else
        out[0] = '\0';
    out[out_size - 1] = '\0';
}

static void relocate_selected(Model *out_model, const char *prev_name)
{
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

static void resort_and_relocate(Model *out_model, const char *prev_name)
{
    sort_entries(out_model->entries, out_model->entry_count, out_model->sort_mode, out_model->group_mode);
    relocate_selected(out_model, prev_name);
}

static void member_to_entry(const ArchiveMember *m, Entry *e)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(e->name, NAME_MAX_LEN + 1, "%s", m->path);
#pragma GCC diagnostic pop
    memset(&e->st, 0, sizeof(e->st));
    e->st.st_mode = m->is_dir ? S_IFDIR : S_IFREG;
    e->st.st_size = m->size;
    e->st.st_mtime = m->mtime;
}

static void populate_entries_from_level(Model *out_model, const ArchiveLevel *level)
{
    static ArchiveMember children[MAX_ENTRIES];
    int count = archive_children_at(level->members, level->member_count,
                                     level->subfolder, out_model->show_hidden,
                                     children, MAX_ENTRIES);
    out_model->unfiltered_count = count;
    for (int i = 0; i < count; i++)
        member_to_entry(&children[i], &out_model->unfiltered_entries[i]);

    int truncated;
    apply_filter(out_model->unfiltered_entries, out_model->unfiltered_count,
                 out_model->filter_type, out_model->filter_pattern, 1,
                 out_model->sort_mode, out_model->group_mode,
                 out_model->entries, MAX_ENTRIES, &out_model->entry_count, &truncated);
}

static void handle_nav(const Msg *msg, Model *out_model, Cmd *out_cmd)
{
    switch (msg->type) {
    case MSG_RENAME:
        if (out_model->archive_depth > 0) {
            block_in_archive(out_model);
            break;
        }
        if (out_model->selected < out_model->entry_count &&
            !is_protected_name(out_model->entries[out_model->selected].name))
            start_edit(out_model, MODE_RENAME);
        break;

    case MSG_NEW:
        if (out_model->archive_depth > 0) {
            block_in_archive(out_model);
            break;
        }
        start_edit(out_model, MODE_CREATE);
        break;

    case MSG_RUN_CMD:
        if (out_model->archive_depth > 0) {
            block_in_archive(out_model);
            break;
        }
        start_edit(out_model, MODE_RUN_CMD);
        break;

    case MSG_CANCEL:
        out_model->yank_path[0] = '\0';
        out_model->yank_from_archive = 0;
        break;

    case MSG_FILTER_PLAIN:
    case MSG_FILTER_REGEX:
        out_model->mode = MODE_FILTER;
        out_model->filter_type = (msg->type == MSG_FILTER_PLAIN) ? FILTER_PLAIN : FILTER_REGEX;
        out_model->glob_type = GLOB_NONE;
        out_model->glob_pattern[0] = '\0';
        strncpy(out_model->edit_buf, out_model->filter_pattern, sizeof(out_model->edit_buf) - 1);
        out_model->edit_buf[sizeof(out_model->edit_buf) - 1] = '\0';
        out_model->edit_len = strlen(out_model->edit_buf);
        break;

    case MSG_GLOB_PLAIN:
    case MSG_GLOB_REGEX: {
        int had_no_prior_glob = (out_model->glob_type == GLOB_NONE);
        out_model->mode = MODE_GLOB;
        out_model->glob_type = (msg->type == MSG_GLOB_PLAIN) ? GLOB_PLAIN : GLOB_REGEX;
        out_model->filter_type = FILTER_NONE;
        out_model->filter_pattern[0] = '\0';
        strncpy(out_model->edit_buf, out_model->glob_pattern, sizeof(out_model->edit_buf) - 1);
        out_model->edit_buf[sizeof(out_model->edit_buf) - 1] = '\0';
        out_model->edit_len = strlen(out_model->edit_buf);

        if (had_no_prior_glob)
            out_model->entry_count = 0;
        break;
    }

    case MSG_TOGGLE_SELECT_MODE:
        if (out_model->glob_type != GLOB_NONE) {
            block_select_on_glob(out_model);
            break;
        }
        out_model->mode = MODE_SELECT;
        break;

    case MSG_DELETE:
    case MSG_DELETE_PERMANENT:
        if (out_model->archive_depth > 0) {
            block_in_archive(out_model);
            break;
        }
        if (out_model->selected < out_model->entry_count &&
            !is_protected_name(out_model->entries[out_model->selected].name)) {
            out_model->mode = MODE_CONFIRM_DELETE;
            out_model->confirm_permanent_delete = (msg->type == MSG_DELETE_PERMANENT);
        }
        break;

    case MSG_YANK_MOVE:
        if (out_model->archive_depth > 0) {
            block_in_archive(out_model);
            break;
        }
        if (out_model->selected < out_model->entry_count &&
            !is_protected_name(out_model->entries[out_model->selected].name)) {
            join_path(out_model->current_path, out_model->entries[out_model->selected].name,
                      out_model->yank_path, sizeof(out_model->yank_path));
            out_model->yank_is_move = 1;
            out_model->yank_from_archive = 0;
        }
        break;

    case MSG_YANK_COPY:
        if (out_model->selected < out_model->entry_count &&
            !is_protected_name(out_model->entries[out_model->selected].name)) {
            Entry *e = &out_model->entries[out_model->selected];

            if (out_model->archive_depth > 0) {
                if (S_ISDIR(e->st.st_mode)) {
                    block_in_archive(out_model);
                    break;
                }

                ArchiveLevel *level = &out_model->archive_stack[out_model->archive_depth - 1];
                out_model->yank_path[0] = '\0';
                out_model->yank_is_move = 0;
                out_model->yank_from_archive = 1;
                out_model->yank_archive_format = level->format;
                strncpy(out_model->yank_archive_source_path, level->source_path,
                        sizeof(out_model->yank_archive_source_path) - 1);
                out_model->yank_archive_source_path[sizeof(out_model->yank_archive_source_path) - 1] = '\0';
                append_subfolder_segment(level->subfolder, e->name,
                                          out_model->yank_archive_member_path,
                                          sizeof(out_model->yank_archive_member_path));
            } else {
                join_path(out_model->current_path, e->name, out_model->yank_path, sizeof(out_model->yank_path));
                out_model->yank_is_move = 0;
                out_model->yank_from_archive = 0;
            }
        }
        break;

    case MSG_PASTE: {
        if (out_model->archive_depth > 0) {
            block_in_archive(out_model);
            break;
        }

        if (out_model->yank_from_archive) {
            char member_name[NAME_MAX_LEN + 1];
            basename_of(out_model->yank_archive_member_path, member_name, sizeof(member_name));

            char resolved_name[NAME_MAX_LEN + 1];
            find_available_name(member_name, out_model->unfiltered_entries, out_model->unfiltered_count,
                                 resolved_name, sizeof(resolved_name));

            out_cmd->type = CMD_EXTRACT_MEMBER_TO;
            out_cmd->archive_format = out_model->yank_archive_format;
            strncpy(out_cmd->path, out_model->yank_archive_source_path, sizeof(out_cmd->path) - 1);
            out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
            strncpy(out_cmd->path2, out_model->yank_archive_member_path, sizeof(out_cmd->path2) - 1);
            out_cmd->path2[sizeof(out_cmd->path2) - 1] = '\0';
            join_path(out_model->current_path, resolved_name, out_cmd->path3, sizeof(out_cmd->path3));

            out_model->yank_from_archive = 0;
            out_model->yank_archive_source_path[0] = '\0';
            out_model->yank_archive_member_path[0] = '\0';
            break;
        }

        if (out_model->yank_path[0] == '\0')
            break;

        const char *slash = strrchr(out_model->yank_path, '/');
        const char *base_name = slash ? slash + 1 : out_model->yank_path;

        char resolved_name[NAME_MAX_LEN + 1];
        find_available_name(base_name, out_model->unfiltered_entries, out_model->unfiltered_count,
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

        if (out_model->archive_depth > 0) {
            ArchiveLevel *level = &out_model->archive_stack[out_model->archive_depth - 1];
            populate_entries_from_level(out_model, level);
            break;
        }

        out_cmd->type = CMD_LOAD_DIR;
        out_cmd->show_hidden = out_model->show_hidden;
        strncpy(out_cmd->path, out_model->current_path, sizeof(out_cmd->path) - 1);
        out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
        break;

    case MSG_QUIT:
        out_model->should_quit = 1;
        break;

    case MSG_MOVE_UP:
        move_cursor(out_model, -1);
        break;

    case MSG_MOVE_DOWN:
        move_cursor(out_model, 1);
        break;

    case MSG_GO_PARENT: {
        out_model->filter_type = FILTER_NONE;
        out_model->filter_pattern[0] = '\0';
        out_model->glob_type = GLOB_NONE;
        out_model->glob_pattern[0] = '\0';

        if (out_model->archive_depth == 0) {
            out_cmd->type = CMD_LOAD_DIR;
            out_cmd->show_hidden = out_model->show_hidden;
            parent_path(out_model->current_path, out_cmd->path, sizeof(out_cmd->path));
            break;
        }

        char new_path[PATH_MAX_LEN];
        parent_path(out_model->current_path, new_path, sizeof(new_path));

        ArchiveLevel *level = &out_model->archive_stack[out_model->archive_depth - 1];
        if (level->subfolder[0] != '\0') {
            pop_subfolder_segment(level->subfolder);
            strncpy(out_model->current_path, new_path, sizeof(out_model->current_path) - 1);
            out_model->current_path[sizeof(out_model->current_path) - 1] = '\0';
            populate_entries_from_level(out_model, level);
            out_model->selected = 0;
            recompute_scroll(out_model);
            break;
        }

        if (level->source_is_tmp) {
            char tmp_dir[PATH_MAX_LEN];
            snprintf(tmp_dir, sizeof(tmp_dir), "%s", level->source_path);
            char *slash = strrchr(tmp_dir, '/');
            if (slash)
                *slash = '\0';
            remove(level->source_path);
            rmdir(tmp_dir);
        }
        free(level->members);
        level->members = NULL;
        out_model->archive_depth--;

        if (out_model->archive_depth > 0) {
            ArchiveLevel *outer = &out_model->archive_stack[out_model->archive_depth - 1];
            strncpy(out_model->current_path, new_path, sizeof(out_model->current_path) - 1);
            out_model->current_path[sizeof(out_model->current_path) - 1] = '\0';
            populate_entries_from_level(out_model, outer);
            out_model->selected = 0;
            recompute_scroll(out_model);
        } else {
            out_cmd->type = CMD_LOAD_DIR;
            out_cmd->show_hidden = out_model->show_hidden;
            strncpy(out_cmd->path, new_path, sizeof(out_cmd->path) - 1);
            out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
        }
        break;
    }

    case MSG_PREVIEW: {
        if (out_model->selected >= out_model->entry_count)
            break;

        Entry *e = &out_model->entries[out_model->selected];
        if (S_ISREG(e->st.st_mode)) {
            if (out_model->archive_depth > 0) {
                ArchiveLevel *level = &out_model->archive_stack[out_model->archive_depth - 1];
                out_cmd->type = CMD_PREVIEW_ARCHIVE_MEMBER;
                out_cmd->archive_format = level->format;
                strncpy(out_cmd->path, level->source_path, sizeof(out_cmd->path) - 1);
                out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
                append_subfolder_segment(level->subfolder, e->name, out_cmd->path2, sizeof(out_cmd->path2));
            } else {
                out_cmd->type = CMD_PREVIEW;
                join_path(out_model->current_path, e->name, out_cmd->path, sizeof(out_cmd->path));
            }
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
            out_model->filter_type = FILTER_NONE;
            out_model->filter_pattern[0] = '\0';
            out_model->glob_type = GLOB_NONE;
            out_model->glob_pattern[0] = '\0';

            if (out_model->archive_depth > 0) {
                ArchiveLevel *level = &out_model->archive_stack[out_model->archive_depth - 1];
                char new_subfolder[PATH_MAX_LEN];
                append_subfolder_segment(level->subfolder, e->name, new_subfolder, sizeof(new_subfolder));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                snprintf(level->subfolder, sizeof(level->subfolder), "%s", new_subfolder);
#pragma GCC diagnostic pop

                char new_path[PATH_MAX_LEN];
                join_path(out_model->current_path, e->name, new_path, sizeof(new_path));
                strncpy(out_model->current_path, new_path, sizeof(out_model->current_path) - 1);
                out_model->current_path[sizeof(out_model->current_path) - 1] = '\0';

                populate_entries_from_level(out_model, level);
                out_model->selected = 0;
                recompute_scroll(out_model);
            } else {
                out_cmd->type = CMD_LOAD_DIR;
                out_cmd->show_hidden = out_model->show_hidden;
                join_path(out_model->current_path, e->name, out_cmd->path, sizeof(out_cmd->path));
            }
        } else if (S_ISREG(e->st.st_mode)) {
            ArchiveFormat fmt = archive_format_for_name(e->name);
            if (fmt != ARCHIVE_NONE) {
                out_model->filter_type = FILTER_NONE;
                out_model->filter_pattern[0] = '\0';
                out_model->glob_type = GLOB_NONE;
                out_model->glob_pattern[0] = '\0';

                if (out_model->archive_depth > 0) {
                    if (out_model->archive_depth >= ARCHIVE_MAX_DEPTH)
                        break;

                    ArchiveLevel *level = &out_model->archive_stack[out_model->archive_depth - 1];
                    out_cmd->type = CMD_EXTRACT_MEMBER;
                    out_cmd->archive_format = level->format;
                    strncpy(out_cmd->path, level->source_path, sizeof(out_cmd->path) - 1);
                    out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
                    append_subfolder_segment(level->subfolder, e->name, out_cmd->path2, sizeof(out_cmd->path2));
                } else {
                    out_cmd->type = CMD_LIST_ARCHIVE;
                    out_cmd->archive_format = fmt;
                    out_cmd->path2[0] = '\0';
                    out_cmd->is_dir = 0;
                    join_path(out_model->current_path, e->name, out_cmd->path, sizeof(out_cmd->path));
                }
            } else if (out_model->archive_depth > 0) {
                ArchiveLevel *level = &out_model->archive_stack[out_model->archive_depth - 1];
                out_cmd->type = CMD_OPEN_ARCHIVE_MEMBER;
                out_cmd->archive_format = level->format;
                strncpy(out_cmd->path, level->source_path, sizeof(out_cmd->path) - 1);
                out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
                append_subfolder_segment(level->subfolder, e->name, out_cmd->path2, sizeof(out_cmd->path2));
            } else {
                out_cmd->type = CMD_LAUNCH_EDITOR;
                join_path(out_model->current_path, e->name, out_cmd->path, sizeof(out_cmd->path));
            }
        }
        break;
    }

    default:
        break;
    }
}

static void recompute_filter_live(Model *out_model)
{
    int truncated;
    apply_filter(out_model->unfiltered_entries, out_model->unfiltered_count,
                 out_model->filter_type, out_model->edit_buf, 1,
                 out_model->sort_mode, out_model->group_mode,
                 out_model->entries, MAX_ENTRIES, &out_model->entry_count, &truncated);
    out_model->selected = 0;
    recompute_scroll(out_model);
}

static void set_recalled_command(Model *out_model, const char *cmd)
{
    out_model->edit_buf[0] = '!';
    strncpy(out_model->edit_buf + 1, cmd, sizeof(out_model->edit_buf) - 2);
    out_model->edit_buf[sizeof(out_model->edit_buf) - 1] = '\0';
    out_model->edit_len = strlen(out_model->edit_buf);
}

static void handle_recall_prev(Model *out_model)
{
    if (!out_model->history)
        return;

    const CommandArena *arena = history_lookup(out_model->history, out_model->current_path);
    if (!arena)
        return;

    HistoryArenaState state = history_arena_state(arena->data, HISTORY_ARENA_BYTES);
    if (state.count == 0)
        return;

    if (out_model->recall_cursor == 0) {
        strncpy(out_model->recall_stash, out_model->edit_buf, sizeof(out_model->recall_stash) - 1);
        out_model->recall_stash[sizeof(out_model->recall_stash) - 1] = '\0';
        out_model->recall_stash_len = out_model->edit_len;
        out_model->recall_cursor = 1;
    } else if (out_model->recall_cursor < state.count) {
        out_model->recall_cursor++;
    } else {
        return;
    }

    const char *cmd = history_arena_command_at(arena->data, HISTORY_ARENA_BYTES, out_model->recall_cursor - 1);
    if (cmd)
        set_recalled_command(out_model, cmd);
}

static void handle_recall_next(Model *out_model)
{
    if (out_model->recall_cursor == 0)
        return;

    if (out_model->recall_cursor == 1) {
        strncpy(out_model->edit_buf, out_model->recall_stash, sizeof(out_model->edit_buf) - 1);
        out_model->edit_buf[sizeof(out_model->edit_buf) - 1] = '\0';
        out_model->edit_len = out_model->recall_stash_len;
        out_model->recall_cursor = 0;
        return;
    }

    out_model->recall_cursor--;

    if (!out_model->history)
        return;

    const CommandArena *arena = history_lookup(out_model->history, out_model->current_path);
    if (!arena)
        return;

    const char *cmd = history_arena_command_at(arena->data, HISTORY_ARENA_BYTES, out_model->recall_cursor - 1);
    if (cmd)
        set_recalled_command(out_model, cmd);
}

static void handle_edit(const Msg *msg, Model *out_model, Cmd *out_cmd)
{
    switch (msg->type) {
    case MSG_RECALL_PREV:
        if (out_model->mode == MODE_RUN_CMD)
            handle_recall_prev(out_model);
        break;

    case MSG_RECALL_NEXT:
        if (out_model->mode == MODE_RUN_CMD)
            handle_recall_next(out_model);
        break;

    case MSG_CANCEL:
        if (out_model->mode == MODE_FILTER) {
            out_model->filter_type = FILTER_NONE;
            out_model->filter_pattern[0] = '\0';
            recompute_filter_live(out_model);
        } else if (out_model->mode == MODE_GLOB) {
            out_model->glob_type = GLOB_NONE;
            out_model->glob_pattern[0] = '\0';
            recompute_filter_live(out_model);
        }
        cancel_edit(out_model);
        break;

    case MSG_DELETE:
        if (out_model->edit_len > 0)
            out_model->edit_buf[--out_model->edit_len] = '\0';
        if (out_model->mode == MODE_FILTER)
            recompute_filter_live(out_model);
        break;

    case MSG_TEXT_INPUT:
        if (out_model->edit_len < NAME_MAX_LEN) {
            out_model->edit_buf[out_model->edit_len++] = msg->ch;
            out_model->edit_buf[out_model->edit_len] = '\0';
        }
        if (out_model->mode == MODE_FILTER)
            recompute_filter_live(out_model);
        break;

    case MSG_ACTIVATE:
        if (out_model->mode == MODE_FILTER) {
            strncpy(out_model->filter_pattern, out_model->edit_buf, sizeof(out_model->filter_pattern) - 1);
            out_model->filter_pattern[sizeof(out_model->filter_pattern) - 1] = '\0';
            cancel_edit(out_model);
            break;
        }

        if (out_model->mode == MODE_GLOB) {
            strncpy(out_model->glob_pattern, out_model->edit_buf, sizeof(out_model->glob_pattern) - 1);
            out_model->glob_pattern[sizeof(out_model->glob_pattern) - 1] = '\0';
            cancel_edit(out_model);

            if (out_model->archive_depth > 0) {
                ArchiveLevel *level = &out_model->archive_stack[out_model->archive_depth - 1];
                FilterType filter_type = (out_model->glob_type == GLOB_REGEX) ? FILTER_REGEX : FILTER_PLAIN;

                static ArchiveMember matches[MAX_ENTRIES];
                int truncated = 0;
                int count = archive_glob_matches(level->members, level->member_count,
                                                  level->subfolder, filter_type, out_model->glob_pattern,
                                                  matches, MAX_ENTRIES, &truncated);

                out_model->entry_count = count;
                for (int i = 0; i < count; i++)
                    member_to_entry(&matches[i], &out_model->entries[i]);
                sort_entries(out_model->entries, out_model->entry_count, out_model->sort_mode, out_model->group_mode);
                out_model->glob_capped = truncated;
                out_model->selected = 0;
                recompute_scroll(out_model);
                break;
            }

            out_cmd->type = CMD_BUILD_GLOB;
            out_cmd->glob_type = out_model->glob_type;
            strncpy(out_cmd->cmd_text, out_model->glob_pattern, sizeof(out_cmd->cmd_text) - 1);
            out_cmd->cmd_text[sizeof(out_cmd->cmd_text) - 1] = '\0';
            strncpy(out_cmd->path, out_model->current_path, sizeof(out_cmd->path) - 1);
            out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
            break;
        }

        if (out_model->edit_len == 0) {
            cancel_edit(out_model);
            break;
        }

        if (out_model->mode == MODE_RENAME) {
            out_cmd->type = CMD_RENAME;
            join_path(out_model->current_path, out_model->entries[out_model->selected].name,
                      out_cmd->path, sizeof(out_cmd->path));

            char rename_dir[PATH_MAX_LEN];
            dirname_of(out_model->entries[out_model->selected].name, out_model->current_path,
                       rename_dir, sizeof(rename_dir));
            join_path(rename_dir, out_model->edit_buf, out_cmd->path2, sizeof(out_cmd->path2));

            if (strcmp(out_cmd->path, out_model->yank_path) == 0)
                out_model->yank_path[0] = '\0';
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
    int was_batch = out_model->marked_count > 0;
    out_model->mode = MODE_NAV;

    if (msg->type != MSG_TEXT_INPUT || (msg->ch != 'y' && msg->ch != 'Y'))
        return;

    if (was_batch) {
        out_cmd->type = out_model->confirm_permanent_delete ? CMD_DELETE : CMD_TRASH;
        out_cmd->batch_count = 0;

        for (int i = 0; i < out_model->marked_count && out_cmd->batch_count < MAX_ENTRIES; i++) {
            const MarkedItem *marked_item = &out_model->marked_items[i];
            char name[NAME_MAX_LEN + 1];
            basename_of(marked_item->path, name, sizeof(name));
            if (is_protected_name(name))
                continue;

            CmdBatchItem *item = &out_cmd->batch_items[out_cmd->batch_count];
            snprintf(item->path, sizeof(item->path), "%s", marked_item->path);
            item->is_dir = marked_item->is_dir;

            if (strcmp(item->path, out_model->yank_path) == 0)
                out_model->yank_path[0] = '\0';

            out_cmd->batch_count++;
        }

        memset(out_model->marked, 0, sizeof(out_model->marked));
        out_model->marked_count = 0;
        out_model->marked_dir[0] = '\0';
        out_model->range_active = 0;
        return;
    }

    Entry *e = &out_model->entries[out_model->selected];
    out_cmd->type = out_model->confirm_permanent_delete ? CMD_DELETE : CMD_TRASH;
    out_cmd->is_dir = S_ISDIR(e->st.st_mode);
    join_path(out_model->current_path, e->name, out_cmd->path, sizeof(out_cmd->path));

    if (strcmp(out_cmd->path, out_model->yank_path) == 0)
        out_model->yank_path[0] = '\0';
}

static void handle_glob_built(const Msg *msg, Model *out_model)
{
    out_model->entry_count = msg->glob_built.entry_count;
    memcpy(out_model->entries, msg->glob_built.entries,
           sizeof(Entry) * msg->glob_built.entry_count);
    sort_entries(out_model->entries, out_model->entry_count, out_model->sort_mode, out_model->group_mode);
    out_model->glob_capped = msg->glob_built.truncated;
    out_model->selected = 0;
    recompute_scroll(out_model);
}

static void handle_archive_listed(const Msg *msg, Model *out_model)
{
    if (out_model->archive_depth >= ARCHIVE_MAX_DEPTH)
        return;

    ArchiveLevel *level = &out_model->archive_stack[out_model->archive_depth];
    level->format = msg->archive_listed.format;
    level->source_is_tmp = msg->archive_listed.source_is_tmp;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    if (level->source_is_tmp) {
        snprintf(level->display_name, sizeof(level->display_name), "%s", msg->archive_listed.display_name);
    } else {
        basename_of(msg->archive_listed.path, level->display_name, sizeof(level->display_name));
    }
    snprintf(level->source_path, sizeof(level->source_path), "%s", msg->archive_listed.path);
#pragma GCC diagnostic pop
    level->subfolder[0] = '\0';

    level->member_count = msg->archive_listed.member_count;
    if (level->member_count > MAX_ENTRIES)
        level->member_count = MAX_ENTRIES;

    free(level->members);
    level->members = malloc(sizeof(ArchiveMember) * level->member_count);
    memcpy(level->members, msg->archive_listed.members, sizeof(ArchiveMember) * level->member_count);

    out_model->archive_depth++;

    if (level->source_is_tmp) {
        char new_path[PATH_MAX_LEN];
        join_path(out_model->current_path, level->display_name, new_path, sizeof(new_path));
        strncpy(out_model->current_path, new_path, sizeof(out_model->current_path) - 1);
        out_model->current_path[sizeof(out_model->current_path) - 1] = '\0';
    } else {
        strncpy(out_model->current_path, msg->archive_listed.path, sizeof(out_model->current_path) - 1);
        out_model->current_path[sizeof(out_model->current_path) - 1] = '\0';
    }

    populate_entries_from_level(out_model, level);
    out_model->selected = 0;
    recompute_scroll(out_model);
}

static void handle_member_extracted(const Msg *msg, Model *out_model, Cmd *out_cmd)
{
    if (out_model->archive_depth >= ARCHIVE_MAX_DEPTH)
        return;

    char member_name[NAME_MAX_LEN + 1];
    basename_of(msg->member_extracted.member_path, member_name, sizeof(member_name));

    ArchiveFormat fmt = archive_format_for_name(member_name);
    if (fmt == ARCHIVE_NONE)
        return;

    out_cmd->type = CMD_LIST_ARCHIVE;
    out_cmd->archive_format = fmt;
    strncpy(out_cmd->path, msg->member_extracted.tmp_path, sizeof(out_cmd->path) - 1);
    out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
    strncpy(out_cmd->path2, member_name, sizeof(out_cmd->path2) - 1);
    out_cmd->path2[sizeof(out_cmd->path2) - 1] = '\0';
    out_cmd->is_dir = 1;
}

static void handle_op_succeeded(Model *out_model, Cmd *out_cmd)
{
    if (out_model->archive_depth > 0) {
        ArchiveLevel *level = &out_model->archive_stack[out_model->archive_depth - 1];

        if (out_model->glob_type != GLOB_NONE) {
            FilterType filter_type = (out_model->glob_type == GLOB_REGEX) ? FILTER_REGEX : FILTER_PLAIN;

            static ArchiveMember matches[MAX_ENTRIES];
            int truncated = 0;
            int count = archive_glob_matches(level->members, level->member_count,
                                              level->subfolder, filter_type, out_model->glob_pattern,
                                              matches, MAX_ENTRIES, &truncated);

            out_model->entry_count = count;
            for (int i = 0; i < count; i++)
                member_to_entry(&matches[i], &out_model->entries[i]);
            sort_entries(out_model->entries, out_model->entry_count, out_model->sort_mode, out_model->group_mode);
            out_model->glob_capped = truncated;
            return;
        }

        populate_entries_from_level(out_model, level);
        return;
    }

    if (out_model->glob_type != GLOB_NONE) {
        out_cmd->type = CMD_BUILD_GLOB;
        out_cmd->glob_type = out_model->glob_type;
        strncpy(out_cmd->cmd_text, out_model->glob_pattern, sizeof(out_cmd->cmd_text) - 1);
        out_cmd->cmd_text[sizeof(out_cmd->cmd_text) - 1] = '\0';
        strncpy(out_cmd->path, out_model->current_path, sizeof(out_cmd->path) - 1);
        out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
        return;
    }

    out_cmd->type = CMD_LOAD_DIR;
    out_cmd->show_hidden = out_model->show_hidden;
    strncpy(out_cmd->path, out_model->current_path, sizeof(out_cmd->path) - 1);
    out_cmd->path[sizeof(out_cmd->path) - 1] = '\0';
}

static void handle_dir_loaded(const Msg *msg, Model *out_model)
{
    char prev_name[NAME_MAX_LEN + 1];
    selected_name(out_model, prev_name, sizeof(prev_name));

    out_model->unfiltered_count = msg->dir_loaded.entry_count;
    memcpy(out_model->unfiltered_entries, msg->dir_loaded.entries,
           sizeof(Entry) * msg->dir_loaded.entry_count);
    strncpy(out_model->current_path, msg->dir_loaded.path,
            sizeof(out_model->current_path) - 1);
    out_model->current_path[sizeof(out_model->current_path) - 1] = '\0';

    int truncated;
    apply_filter(out_model->unfiltered_entries, out_model->unfiltered_count,
                 out_model->filter_type, out_model->filter_pattern, 1,
                 out_model->sort_mode, out_model->group_mode,
                 out_model->entries, MAX_ENTRIES, &out_model->entry_count, &truncated);
    relocate_selected(out_model, prev_name);
}

static void ensure_marks_scope(Model *out_model)
{
    if (strcmp(out_model->marked_dir, out_model->current_path) == 0)
        return;

    memset(out_model->marked, 0, sizeof(out_model->marked));
    out_model->marked_count = 0;
    out_model->range_active = 0;
    strncpy(out_model->marked_dir, out_model->current_path, sizeof(out_model->marked_dir) - 1);
    out_model->marked_dir[sizeof(out_model->marked_dir) - 1] = '\0';
}

static void handle_select(const Msg *msg, Model *out_model, Cmd *out_cmd)
{
    switch (msg->type) {
    case MSG_TOGGLE_SELECT_MODE:
    case MSG_CANCEL:
        out_model->mode = MODE_NAV;
        memset(out_model->marked, 0, sizeof(out_model->marked));
        out_model->marked_count = 0;
        out_model->range_active = 0;
        out_model->marked_dir[0] = '\0';
        break;

    case MSG_GO_PARENT:
    case MSG_ACTIVATE:
        handle_nav(msg, out_model, out_cmd);
        break;

    case MSG_DELETE:
    case MSG_DELETE_PERMANENT:
        if (out_model->marked_count == 0) {
            handle_nav(msg, out_model, out_cmd);
            break;
        }
        if (out_model->archive_depth > 0) {
            block_in_archive(out_model);
            break;
        }
        out_model->mode = MODE_CONFIRM_DELETE;
        out_model->confirm_permanent_delete = (msg->type == MSG_DELETE_PERMANENT);
        break;

    case MSG_CYCLE_SORT:
    case MSG_CYCLE_GROUP:
    case MSG_FILTER_PLAIN:
    case MSG_FILTER_REGEX:
    case MSG_GLOB_PLAIN:
    case MSG_GLOB_REGEX:
        if (out_model->marked_count > 0)
            break;
        handle_nav(msg, out_model, out_cmd);
        break;

    case MSG_TOGGLE_MARK:
        ensure_marks_scope(out_model);
        if (out_model->selected < out_model->entry_count)
            set_mark(out_model, out_model->selected, !out_model->marked[out_model->selected]);
        break;

    case MSG_TOGGLE_MARK_ALL:
        ensure_marks_scope(out_model);
        if (out_model->marked_count < out_model->entry_count) {
            for (int i = 0; i < out_model->entry_count; i++)
                set_mark(out_model, i, 1);
        } else {
            memset(out_model->marked, 0, sizeof(out_model->marked));
            out_model->marked_count = 0;
        }
        break;

    case MSG_TOGGLE_RANGE_SELECT:
        if (out_model->range_active) {
            out_model->range_active = 0;
            break;
        }
        ensure_marks_scope(out_model);
        if (out_model->selected < out_model->entry_count) {
            int target = !out_model->marked[out_model->selected];
            out_model->range_active = 1;
            out_model->range_target = (unsigned char)target;
            set_mark(out_model, out_model->selected, target);
        }
        break;

    case MSG_MOVE_UP:
        move_cursor(out_model, -1);
        if (out_model->range_active)
            set_mark(out_model, out_model->selected, out_model->range_target);
        break;

    case MSG_MOVE_DOWN:
        move_cursor(out_model, 1);
        if (out_model->range_active)
            set_mark(out_model, out_model->selected, out_model->range_target);
        break;

    default:
        break;
    }
}

void update(const Msg *msg, const Model *model, Model *out_model, Cmd *out_cmd)
{
    *out_model = *model;
    out_cmd->type = CMD_NONE;
    out_cmd->batch_count = 0;

    switch (msg->type) {
    case MSG_DIR_LOADED:
        handle_dir_loaded(msg, out_model);
        return;

    case MSG_GLOB_BUILT:
        handle_glob_built(msg, out_model);
        return;

    case MSG_ARCHIVE_LISTED:
        handle_archive_listed(msg, out_model);
        return;

    case MSG_MEMBER_EXTRACTED:
        handle_member_extracted(msg, out_model, out_cmd);
        return;

    case MSG_OP_SUCCEEDED:
        handle_op_succeeded(out_model, out_cmd);
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
    else if (model->mode == MODE_RENAME || model->mode == MODE_CREATE ||
             model->mode == MODE_RUN_CMD || model->mode == MODE_FILTER ||
             model->mode == MODE_GLOB)
        handle_edit(msg, out_model, out_cmd);
    else if (model->mode == MODE_CONFIRM_DELETE)
        handle_confirm_delete(msg, out_model, out_cmd);
    else if (model->mode == MODE_SELECT)
        handle_select(msg, out_model, out_cmd);
    else if (model->mode == MODE_ERROR) {
        out_model->mode = MODE_NAV;
    }
}

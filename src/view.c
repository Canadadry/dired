#include "view.h"
#include "helpers.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define HELP_TEXT \
    "up/down: Navigate  left: Parent  right/Enter: Open  r: Rename  " \
    "n: New  space: Preview  c: Yank copy  m: Yank move  " \
    "p: Paste  s: Sort  d: Group  o: Next page  a: Toggle hidden  Backspace: Trash  x: Delete  " \
    ": Run command  Up/Down (in command prompt): Recall history  " \
    "f: Filter  F: Filter (regex)  g: Glob  G: Glob (regex)  Esc: Cancel yank  q: Quit"

static void add_line(View *v, StyleTag style, const char *fmt, ...)
{
    Line *line = &v->lines[v->line_count++];
    line->style = style;

    va_list args;
    va_start(args, fmt);
    vsnprintf(line->text, sizeof(line->text), fmt, args);
    va_end(args);
}

static void add_prompt_line(View *v, const Model *m)
{
    switch (m->mode) {
    case MODE_CREATE:
        add_line(v, STYLE_NORMAL, "Create:");
        break;
    case MODE_RENAME:
        add_line(v, STYLE_NORMAL, "Rename:");
        break;
    case MODE_RUN_CMD:
        add_line(v, STYLE_NORMAL, ":%s", m->edit_buf);
        break;
    case MODE_FILTER: {
        const char *prefix = (m->filter_type == FILTER_REGEX) ? "F:" : "f:";
        StyleTag style = filter_is_valid(m->filter_type, m->edit_buf) ? STYLE_VALID : STYLE_ERROR;
        add_line(v, style, "%s%s", prefix, m->edit_buf);
        break;
    }
    case MODE_GLOB: {
        const char *prefix = (m->glob_type == GLOB_REGEX) ? "G:" : "g:";
        FilterType ft = (m->glob_type == GLOB_REGEX) ? FILTER_REGEX : FILTER_PLAIN;
        StyleTag style = filter_is_valid(ft, m->edit_buf) ? STYLE_VALID : STYLE_ERROR;
        add_line(v, style, "%s%s", prefix, m->edit_buf);
        break;
    }
    case MODE_CONFIRM_DELETE:
        if (m->confirm_permanent_delete)
            add_line(v, STYLE_PROMPT, "Delete '%s' ? [y/N]", m->entries[m->selected].name);
        else
            add_line(v, STYLE_PROMPT, "Move '%s' to trash? [y/N]", m->entries[m->selected].name);
        break;
    case MODE_ERROR:
        add_line(v, STYLE_ERROR, "%s", m->error_msg);
        break;
    default:
        if (m->yank_count == 1) {
            const char *slash = strrchr(m->yank_paths[0], '/');
            const char *name = slash ? slash + 1 : m->yank_paths[0];
            add_line(v, STYLE_NORMAL, "Yanked: %s (%s)", name, m->yank_is_move ? "move" : "copy");
        } else if (m->yank_count > 1) {
            add_line(v, STYLE_NORMAL, "Yanked: %d items (%s)", m->yank_count, m->yank_is_move ? "move" : "copy");
        } else if (m->filter_type != FILTER_NONE) {
            add_line(v, STYLE_NORMAL, "Filter: %s", m->filter_pattern);
        } else if (m->glob_type != GLOB_NONE) {
            if (m->glob_capped)
                add_line(v, STYLE_NORMAL, "Glob: %s (%d+ shown)", m->glob_pattern, MAX_ENTRIES);
            else
                add_line(v, STYLE_NORMAL, "Glob: %s", m->glob_pattern);
        } else {
            add_line(v, STYLE_NORMAL, "");
        }
        break;
    }
}

static StyleTag entry_style_tag(GitStatusTag git_status, int selected, int marked)
{
    if (marked)
        return selected ? STYLE_MARKED_SELECTED : STYLE_MARKED;

    switch (git_status) {
    case GIT_STATUS_CONFLICTED:
        return selected ? STYLE_CONFLICTED_SELECTED : STYLE_CONFLICTED;
    case GIT_STATUS_MODIFIED:
        return selected ? STYLE_MODIFIED_SELECTED : STYLE_MODIFIED;
    case GIT_STATUS_UNTRACKED:
        return selected ? STYLE_UNTRACKED_SELECTED : STYLE_UNTRACKED;
    case GIT_STATUS_DELETED:
        return selected ? STYLE_DELETED_SELECTED : STYLE_DELETED;
    case GIT_STATUS_IGNORED:
        return selected ? STYLE_IGNORED_SELECTED : STYLE_IGNORED;
    default:
        return selected ? STYLE_SELECTED : STYLE_NORMAL;
    }
}

static void add_entry_line(View *v, const Model *m, int i)
{
    char perms[11];
    mode_to_str(m->entries[i].st.st_mode, perms);
    StyleTag style = entry_style_tag(m->entries[i].git_status, i == m->selected, m->marked[i]);
    add_line(v, style, "%s %8ld %s", perms, (long)m->entries[i].st.st_size, m->entries[i].name);
}

static void append_scroll_marker(Line *line, int term_width, const char *marker)
{
    int pad_to = term_width - (int)strlen(marker);
    if (pad_to < 0)
        pad_to = 0;
    if ((size_t)pad_to >= sizeof(line->text))
        pad_to = (int)sizeof(line->text) - 1;

    size_t len = strlen(line->text);
    if ((int)len != pad_to) {
        if ((int)len < pad_to)
            memset(line->text + len, ' ', (size_t)pad_to - len);
        line->text[pad_to] = '\0';
        len = (size_t)pad_to;
    }
    strncat(line->text, marker, sizeof(line->text) - len - 1);
}

static void add_virtual_line(View *v, const Model *m)
{
    StyleTag style = entry_style_tag(GIT_STATUS_NONE, m->entry_count == m->selected, 0);
    add_line(v, style, "          %s", m->edit_buf);
}

View view(const Model *model)
{
    View v;
    v.line_count = 0;

    if (model->mode == MODE_SELECT) {
        if (model->marked_count > 0)
            add_line(&v, STYLE_NORMAL, "VISUAL(%d) : %s", model->marked_count, model->current_path);
        else
            add_line(&v, STYLE_NORMAL, "VISUAL : %s", model->current_path);
    } else {
        add_line(&v, STYLE_NORMAL, "Path: %s", model->current_path);
    }
    add_prompt_line(&v, model);
    Line *status_line = &v.lines[v.line_count - 1];

    int visible_rows = visible_entry_rows(model->term_height, model_has_virtual_line(model));
    int end = model->scroll_offset + visible_rows;
    if (end > model->entry_count)
        end = model->entry_count;

    for (int i = model->scroll_offset; i < end; i++)
        add_entry_line(&v, model, i);

    int more_above = model->scroll_offset > 0;
    int more_below = end < model->entry_count;
    if (more_above && more_below)
        append_scroll_marker(status_line, model->term_width, "<- ->");
    else if (more_above)
        append_scroll_marker(status_line, model->term_width, "<-");
    else if (more_below)
        append_scroll_marker(status_line, model->term_width, "->");

    if (model_has_virtual_line(model))
        add_virtual_line(&v, model);

    add_line(&v, STYLE_NORMAL, HELP_TEXT);

    return v;
}

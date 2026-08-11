#include "view.h"
#include "helpers.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define HELP_TEXT \
    "up/down: Navigate  left: Parent  right/Enter: Open  r: Rename  " \
    "n: New  space: Preview  c: Yank copy  m: Yank move  " \
    "p: Paste  s: Sort  d: Group  o: Next page  a: Toggle hidden  Backspace: Trash  x: Delete  " \
    ": Run command  f: Filter  F: Filter (regex)  Esc: Cancel yank  q: Quit"

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
        if (m->yank_path[0] != '\0') {
            const char *slash = strrchr(m->yank_path, '/');
            const char *name = slash ? slash + 1 : m->yank_path;
            add_line(v, STYLE_NORMAL, "Yanked: %s (%s)", name, m->yank_is_move ? "move" : "copy");
        } else if (m->filter_type != FILTER_NONE) {
            add_line(v, STYLE_NORMAL, "Filter: %s", m->filter_pattern);
        } else {
            add_line(v, STYLE_NORMAL, "");
        }
        break;
    }
}

static void add_entry_line(View *v, const Model *m, int i)
{
    char perms[11];
    mode_to_str(m->entries[i].st.st_mode, perms);
    StyleTag style = (i == m->selected) ? STYLE_SELECTED : STYLE_NORMAL;
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
    StyleTag style = (m->entry_count == m->selected) ? STYLE_SELECTED : STYLE_NORMAL;
    add_line(v, style, "          %s", m->edit_buf);
}

View view(const Model *model)
{
    View v;
    v.line_count = 0;

    add_line(&v, STYLE_NORMAL, "Path: %s", model->current_path);
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

#include "view.h"
#include "helpers.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define HELP_TEXT \
    "up/down: Navigate  left: Parent  right/Enter: Open  r: Rename  " \
    "n: New  space: Preview  c: Yank copy  m: Yank move  " \
    "p: Paste  Backspace: Delete  q: Quit"

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
    case MODE_CONFIRM_DELETE:
        add_line(v, STYLE_PROMPT, "Delete '%s' ? [y/N]", m->entries[m->selected].name);
        break;
    case MODE_ERROR:
        add_line(v, STYLE_ERROR, "%s", m->error_msg);
        break;
    default:
        if (m->yank_path[0] != '\0') {
            const char *slash = strrchr(m->yank_path, '/');
            const char *name = slash ? slash + 1 : m->yank_path;
            add_line(v, STYLE_NORMAL, "Yanked: %s (%s)", name, m->yank_is_move ? "move" : "copy");
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

    for (int i = 0; i < model->entry_count; i++)
        add_entry_line(&v, model, i);

    if (model_has_virtual_line(model))
        add_virtual_line(&v, model);

    add_line(&v, STYLE_NORMAL, HELP_TEXT);

    return v;
}

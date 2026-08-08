#ifndef DIRED_VIEW_H
#define DIRED_VIEW_H

#include "model.h"

#define LINE_MAX_LEN (NAME_MAX_LEN + 64)
#define VIEW_MAX_LINES (MAX_ENTRIES + 3)

typedef enum {
    STYLE_NORMAL = 0,
    STYLE_SELECTED,
    STYLE_PROMPT,
    STYLE_ERROR,
} StyleTag;

typedef struct {
    char text[LINE_MAX_LEN];
    StyleTag style;
} Line;

/* view() never calls a rendering-library function and never inspects
 * screen size — main() paints lines[0..line_count-2] top-down starting at
 * row 0, and pins the final line (the help/status bar) to the bottom row,
 * whatever that is. */
typedef struct {
    Line lines[VIEW_MAX_LINES];
    int line_count;
} View;

View view(const Model *model);

#endif // DIRED_VIEW_H

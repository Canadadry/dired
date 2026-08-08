#ifndef DIRED_MODEL_H
#define DIRED_MODEL_H

#include <stddef.h>
#include <sys/stat.h>

#define MAX_ENTRIES 1024
#define NAME_MAX_LEN 1024
#define PATH_MAX_LEN 1024

typedef enum {
    MODE_NAV = 0,
    MODE_RENAME,
    MODE_CREATE,
    MODE_CONFIRM_DELETE,
    MODE_ERROR,
} AppMode;

typedef struct {
    char name[NAME_MAX_LEN + 1];
    struct stat st;
} Entry;

typedef struct {
    Entry entries[MAX_ENTRIES];
    int entry_count;
    int selected;

    char current_path[PATH_MAX_LEN];

    AppMode mode;
    char edit_buf[NAME_MAX_LEN + 1];
    size_t edit_len;

    /* Pending yank, independent of `mode` — survives normal navigation.
     * An empty yank_path is the "nothing pending" sentinel. */
    char yank_path[PATH_MAX_LEN];
    int yank_is_move;

    char error_msg[256];

    int should_quit;
} Model;

/* True when a virtual (not-yet-created) row should be appended below the
 * real entries — derived from mode so it can never drift out of sync with
 * it. Whichever entry is selected when MODE_CONFIRM_DELETE is entered is
 * always the delete target: every non-confirming key cancels back to
 * MODE_NAV rather than moving the selection, so `selected` doubles as the
 * pending-delete index without a separate field. */
static inline int model_has_virtual_line(const Model *m)
{
    return m->mode == MODE_CREATE;
}

#endif // DIRED_MODEL_H

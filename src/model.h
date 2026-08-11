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
    MODE_RUN_CMD,
    MODE_FILTER,
} AppMode;

typedef enum {
    FILTER_NONE = 0,
    FILTER_PLAIN,
    FILTER_REGEX,
} FilterType;

/* Fixed 8-state cycle order for the `s` key: name -> date -> size -> ext,
 * ascending before descending within each key, wrapping back to name-asc. */
typedef enum {
    SORT_NAME_ASC = 0,
    SORT_NAME_DESC,
    SORT_DATE_ASC,
    SORT_DATE_DESC,
    SORT_SIZE_ASC,
    SORT_SIZE_DESC,
    SORT_EXT_ASC,
    SORT_EXT_DESC,
    SORT_MODE_COUNT,
} SortMode;

/* 3-state cycle order for the `d` key. */
typedef enum {
    GROUP_DIRS_FIRST = 0,
    GROUP_DIRS_LAST,
    GROUP_MIXED,
    GROUP_MODE_COUNT,
} GroupMode;

typedef struct {
    char name[NAME_MAX_LEN + 1];
    struct stat st;
} Entry;

typedef struct {
    Entry entries[MAX_ENTRIES];
    int entry_count;
    int selected;

    Entry unfiltered_entries[MAX_ENTRIES];
    int unfiltered_count;

    FilterType filter_type;
    char filter_pattern[NAME_MAX_LEN + 1];

    char current_path[PATH_MAX_LEN];

    /* Session-scoped: survive directory navigation like yank_path does, but
     * never persisted — a fresh process always starts at the defaults
     * (SORT_NAME_ASC, GROUP_DIRS_FIRST), which are also the zero values. */
    SortMode sort_mode;
    GroupMode group_mode;
    int show_hidden;

    int term_height;
    int term_width;
    int scroll_offset;

    AppMode mode;
    int confirm_permanent_delete;
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

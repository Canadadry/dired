#ifndef DIRED_MSG_H
#define DIRED_MSG_H

#include "model.h"

/* Msg is the closed set of things update() reacts to. Variants are named
 * for what they mean (MSG_RENAME, MSG_GO_PARENT, ...), never for which key
 * produces them, so update() stays correct if main()'s key->Msg table is
 * ever rebound. MSG_TEXT_INPUT is the one exception: it carries a literal
 * typed character, used both for filename entry and for the delete-confirm
 * y/N answer (a prompt answer, not a rebindable command).
 */
typedef enum {
    MSG_NONE = 0,

    MSG_MOVE_UP,
    MSG_MOVE_DOWN,
    MSG_GO_PARENT,
    MSG_ACTIVATE,     /* open selected entry (nav) or validate edit (edit modes) */
    MSG_CANCEL,
    MSG_DELETE,        /* erase last typed char (edit modes) or ask to delete selected entry (nav) */
    MSG_DELETE_PERMANENT,
    MSG_RENAME,
    MSG_NEW,
    MSG_RUN_CMD,
    MSG_RECALL_PREV,
    MSG_RECALL_NEXT,
    MSG_FILTER_PLAIN,
    MSG_FILTER_REGEX,
    MSG_GLOB_PLAIN,
    MSG_GLOB_REGEX,
    MSG_TOGGLE_SELECT_MODE,
    MSG_TOGGLE_MARK,
    MSG_TOGGLE_MARK_ALL,
    MSG_TOGGLE_RANGE_SELECT,
    MSG_PREVIEW,
    MSG_YANK_COPY,
    MSG_YANK_MOVE,
    MSG_PASTE,
    MSG_CYCLE_SORT,
    MSG_CYCLE_GROUP,
    MSG_CYCLE_PAGE,
    MSG_TOGGLE_HIDDEN,
    MSG_QUIT,
    MSG_TEXT_INPUT,
    MSG_RESIZE,

    /* Cmd-completion messages: main() executes the Cmd update() returned,
     * then feeds the outcome back in as one of these. */
    MSG_DIR_LOADED,
    MSG_GLOB_BUILT,
    MSG_ARCHIVE_LISTED,
    MSG_MEMBER_EXTRACTED,
    MSG_OP_SUCCEEDED,
    MSG_OP_FAILED,
} MsgType;

/* entries points at caller-owned storage valid for the duration of the
 * update() call (Msg stays small this way — embedding a full MAX_ENTRIES
 * array by value would make every Msg megabyte-sized, even a plain
 * arrow-key press). */
typedef struct {
    const Entry *entries;
    int entry_count;
    char path[PATH_MAX_LEN];
} MsgDirLoaded;

typedef struct {
    int width;
    int height;
} MsgResize;

typedef struct {
    const Entry *entries;
    int entry_count;
    int truncated;
} MsgGlobBuilt;

typedef struct {
    const ArchiveMember *members;
    int member_count;
    ArchiveFormat format;
    char path[PATH_MAX_LEN];
    char display_name[NAME_MAX_LEN + 1];
    int source_is_tmp;
} MsgArchiveListed;

typedef struct {
    char tmp_path[PATH_MAX_LEN];
    char member_path[PATH_MAX_LEN];
} MsgMemberExtracted;

typedef struct {
    MsgType type;
    union {
        char ch;                  /* MSG_TEXT_INPUT */
        MsgDirLoaded dir_loaded;  /* MSG_DIR_LOADED */
        MsgGlobBuilt glob_built;
        MsgArchiveListed archive_listed;
        MsgMemberExtracted member_extracted;
        char error[256];          /* MSG_OP_FAILED */
        MsgResize resize;
    };
} Msg;

#endif // DIRED_MSG_H

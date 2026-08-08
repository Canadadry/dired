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
    MSG_RENAME,
    MSG_NEW_FILE,
    MSG_NEW_DIR,
    MSG_PREVIEW,
    MSG_QUIT,
    MSG_TEXT_INPUT,

    /* Cmd-completion messages: main() executes the Cmd update() returned,
     * then feeds the outcome back in as one of these. */
    MSG_DIR_LOADED,
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
    MsgType type;
    union {
        char ch;                  /* MSG_TEXT_INPUT */
        MsgDirLoaded dir_loaded;  /* MSG_DIR_LOADED */
        char error[256];          /* MSG_OP_FAILED */
    };
} Msg;

#endif // DIRED_MSG_H

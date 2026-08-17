#ifndef DIRED_CMD_H
#define DIRED_CMD_H

#include "model.h"

/* Cmd describes exactly one effect for main() to run. update() never
 * performs any of these itself; it only returns a description of what
 * should happen, and main() feeds the outcome back in as a Msg. */
typedef enum {
    CMD_NONE = 0,
    CMD_LOAD_DIR,
    CMD_BUILD_GLOB,
    CMD_RENAME,
    CMD_CREATE_FILE,
    CMD_CREATE_DIR,
    CMD_DELETE,
    CMD_TRASH,
    CMD_LAUNCH_EDITOR,
    CMD_PREVIEW,
    CMD_HELP,
    CMD_LIST_ARCHIVE,
    CMD_EXTRACT_MEMBER,
    CMD_EXTRACT_MEMBER_TO,
    CMD_OPEN_ARCHIVE_MEMBER,
    CMD_PREVIEW_ARCHIVE_MEMBER,
    CMD_COPY,
    CMD_MOVE,
    CMD_RUN,
    CMD_CREATE_ZIP,
    CMD_CREATE_TAR,
    CMD_CREATE_TARGZ,
    CMD_EXTRACT_ARCHIVE,
} CmdType;

typedef struct {
    char path[PATH_MAX_LEN];
    char dest[PATH_MAX_LEN];
    int is_dir;
} CmdBatchItem;

typedef struct {
    CmdType type;
    char path[PATH_MAX_LEN];   /* load/create/delete/launch target, or rename/copy/move source */
    char path2[PATH_MAX_LEN];  /* CMD_RENAME/CMD_COPY/CMD_MOVE destination */
    char path3[PATH_MAX_LEN];
    int is_dir;
    int show_hidden;           /* CMD_LOAD_DIR: forwarded to load_directory() */
    char cmd_text[PATH_MAX_LEN];
    char selected_path[PATH_MAX_LEN];
    GlobType glob_type;
    ArchiveFormat archive_format;

    CmdBatchItem batch_items[MAX_ENTRIES];
    int batch_count;
} Cmd;

#endif // DIRED_CMD_H

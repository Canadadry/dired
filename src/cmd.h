#ifndef DIRED_CMD_H
#define DIRED_CMD_H

#include "model.h"

/* Cmd describes exactly one effect for main() to run. update() never
 * performs any of these itself; it only returns a description of what
 * should happen, and main() feeds the outcome back in as a Msg. */
typedef enum {
    CMD_NONE = 0,
    CMD_LOAD_DIR,
    CMD_RENAME,
    CMD_CREATE_FILE,
    CMD_CREATE_DIR,
    CMD_DELETE,
    CMD_LAUNCH_EDITOR,
    CMD_PREVIEW,
} CmdType;

typedef struct {
    CmdType type;
    char path[PATH_MAX_LEN];   /* load/create/delete/launch target, or rename source */
    char path2[PATH_MAX_LEN];  /* CMD_RENAME destination */
    int is_dir;                /* CMD_DELETE: rmdir vs unlink */
} Cmd;

#endif // DIRED_CMD_H

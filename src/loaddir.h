#ifndef DIRED_LOADDIR_H
#define DIRED_LOADDIR_H

#include "msg.h"

/* Reads path via readdir()/lstat(), skipping "." and ".." (they were
 * already unreachable through activate/rename/delete/yank, and "go to
 * parent" is a dedicated key, not a "." entry). When show_hidden is false,
 * entries matching is_hidden_name() are skipped too. Returns MSG_DIR_LOADED
 * on success or MSG_OP_FAILED on an opendir() error. */
Msg load_directory(const char *path, int show_hidden);

char *read_git_status(const char *path);
char *read_git_prefix(const char *path);

#endif // DIRED_LOADDIR_H

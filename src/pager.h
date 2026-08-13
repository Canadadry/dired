#ifndef DIRED_PAGER_H
#define DIRED_PAGER_H

#include <stddef.h>

typedef int (*PtyAllocFn)(int *master_fd, char *slave_path, size_t slave_path_len);

void run_via_pty(char *const primary_argv[], char *const pager_argv[]);
void run_via_pipe(char *const primary_argv[], char *const pager_argv[]);
void run_paged(char *const primary_argv[], char *const pty_pager_argv[], char *const pipe_pager_argv[]);
void run_paged_with_alloc(char *const primary_argv[], char *const pty_pager_argv[],
                           char *const pipe_pager_argv[], PtyAllocFn alloc_fn);

#endif

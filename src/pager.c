#define _GNU_SOURCE

#include "pager.h"

#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/wait.h>

static int pty_alloc_real(int *master_fd, char *slave_path, size_t slave_path_len)
{
    int fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (fd < 0)
        return -1;

    if (grantpt(fd) != 0 || unlockpt(fd) != 0) {
        close(fd);
        return -1;
    }

    if (ptsname_r(fd, slave_path, slave_path_len) != 0) {
        close(fd);
        return -1;
    }

    *master_fd = fd;
    return 0;
}

static void run_via_pty_fd(char *const primary_argv[], char *const pager_argv[], int master_fd,
                            const char *slave_path)
{
    pid_t pid1 = fork();
    if (pid1 < 0) {
        close(master_fd);
        return;
    }

    if (pid1 == 0) {
        close(master_fd);
        setsid();

        int slave_fd = open(slave_path, O_RDWR);
        if (slave_fd < 0)
            _exit(EXIT_FAILURE);

        struct termios raw_termios;
        if (tcgetattr(slave_fd, &raw_termios) == 0) {
            cfmakeraw(&raw_termios);
            tcsetattr(slave_fd, TCSANOW, &raw_termios);
        }

        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        close(slave_fd);

        execvp(primary_argv[0], primary_argv);
        _exit(EXIT_FAILURE);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        dup2(master_fd, STDIN_FILENO);
        close(master_fd);
        setenv("POSIXLY_CORRECT", "1", 1);
        execvp(pager_argv[0], pager_argv);
        _exit(EXIT_FAILURE);
    }

    close(master_fd);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

void run_via_pty(char *const primary_argv[], char *const pager_argv[])
{
    int master_fd;
    char slave_path[64];
    if (pty_alloc_real(&master_fd, slave_path, sizeof(slave_path)) != 0)
        return;

    run_via_pty_fd(primary_argv, pager_argv, master_fd, slave_path);
}

void run_via_pipe(char *const primary_argv[], char *const pager_argv[])
{
    int fd[2];
    if (pipe(fd) != 0)
        return;

    pid_t pid1 = fork();
    if (pid1 == 0) {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        dup2(fd[1], STDERR_FILENO);
        close(fd[1]);
        execvp(primary_argv[0], primary_argv);
        _exit(EXIT_FAILURE);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(fd[1]);
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);
        setenv("POSIXLY_CORRECT", "1", 1);
        execvp(pager_argv[0], pager_argv);
        _exit(EXIT_FAILURE);
    }

    close(fd[0]);
    close(fd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

void run_paged_with_alloc(char *const primary_argv[], char *const pty_pager_argv[],
                           char *const pipe_pager_argv[], PtyAllocFn alloc_fn)
{
    int master_fd;
    char slave_path[64];
    if (alloc_fn(&master_fd, slave_path, sizeof(slave_path)) == 0) {
        run_via_pty_fd(primary_argv, pty_pager_argv, master_fd, slave_path);
        return;
    }

    run_via_pipe(primary_argv, pipe_pager_argv);
}

void run_paged(char *const primary_argv[], char *const pty_pager_argv[], char *const pipe_pager_argv[])
{
    run_paged_with_alloc(primary_argv, pty_pager_argv, pipe_pager_argv, pty_alloc_real);
}

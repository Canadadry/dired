#define _GNU_SOURCE

#include "pager.h"

#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/wait.h>

void run_via_pty(char *const primary_argv[], char *const pager_argv[])
{
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0)
        return;

    if (grantpt(master_fd) != 0 || unlockpt(master_fd) != 0) {
        close(master_fd);
        return;
    }

    char slave_path[64];
    if (ptsname_r(master_fd, slave_path, sizeof(slave_path)) != 0) {
        close(master_fd);
        return;
    }

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

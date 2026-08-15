#include "minitest.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define SIMULATED_DEFAULT_STACK_BYTES (8 * 1024 * 1024)
#define LAUNCH_TIMEOUT_MS 500
#define LAUNCH_POLL_INTERVAL_MS 10

static const char *dired_binary_path(void)
{
    if (access("build/diredd", X_OK) == 0)
        return "build/diredd";
    return "build/dired";
}

static void test_launch(void)
{
    const char *bin = dired_binary_path();
    if (access(bin, X_OK) != 0) {
        TEST_ERRORF("launch", "binary not found: %s", bin);
        return;
    }

    int outpipe[2];
    if (pipe(outpipe) != 0)
        TEST_FATALF("launch", "pipe: %s", strerror(errno));

    pid_t pid = fork();
    if (pid < 0)
        TEST_FATALF("launch", "fork: %s", strerror(errno));

    if (pid == 0) {
        struct rlimit rl = { SIMULATED_DEFAULT_STACK_BYTES, SIMULATED_DEFAULT_STACK_BYTES };
        setrlimit(RLIMIT_STACK, &rl);

        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        dup2(outpipe[1], STDOUT_FILENO);
        dup2(outpipe[1], STDERR_FILENO);
        close(outpipe[0]);
        close(outpipe[1]);

        execl(bin, bin, (char *)NULL);
        _exit(127);
    }

    close(outpipe[1]);

    int status = 0;
    int exited = 0;
    for (int waited_ms = 0; waited_ms < LAUNCH_TIMEOUT_MS; waited_ms += LAUNCH_POLL_INTERVAL_MS) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            exited = 1;
            break;
        }
        usleep(LAUNCH_POLL_INTERVAL_MS * 1000);
    }

    if (!exited) {
        kill(pid, SIGTERM);
        waitpid(pid, &status, 0);
    }

    char output[4096];
    ssize_t n = read(outpipe[0], output, sizeof(output) - 1);
    if (n < 0)
        n = 0;
    output[n] = '\0';
    close(outpipe[0]);

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        if (sig == SIGSEGV || sig == SIGABRT || sig == SIGBUS) {
            TEST_ERRORF("launch", "%s died from signal %d (%s) under an %d MiB stack; output:\n%s",
                        bin, sig, strsignal(sig), SIMULATED_DEFAULT_STACK_BYTES / (1024 * 1024), output);
        }
    }

    if (strstr(output, "AddressSanitizer") != NULL) {
        TEST_ERRORF("launch", "%s reported a sanitizer error; output:\n%s", bin, output);
    }
}

void test_launch_group(void)
{
    test_launch();
}

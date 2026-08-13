#include "loaddir.h"
#include "gitstatus.h"
#include "helpers.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define GIT_STATUS_BUF_LEN (1 << 20)

static void quote_shell_arg(const char *arg, char *out, size_t out_size)
{
    size_t i = 0;
    if (i < out_size - 1)
        out[i++] = '\'';
    for (const char *p = arg; *p && i < out_size - 5; p++) {
        if (*p == '\'') {
            out[i++] = '\'';
            out[i++] = '\\';
            out[i++] = '\'';
            out[i++] = '\'';
        } else {
            out[i++] = *p;
        }
    }
    if (i < out_size - 1)
        out[i++] = '\'';
    out[i] = '\0';
}

static char *read_git_status(const char *path)
{
    static char buf[GIT_STATUS_BUF_LEN];

    char quoted_path[PATH_MAX_LEN * 4];
    quote_shell_arg(path, quoted_path, sizeof(quoted_path));

    char cmd[sizeof(quoted_path) + 64];
    snprintf(cmd, sizeof(cmd), "git -C %s status --porcelain --ignored -uall 2>/dev/null", quoted_path);

    FILE *fp = popen(cmd, "r");
    if (!fp)
        return NULL;

    size_t len = 0;
    while (len < sizeof(buf) - 1) {
        size_t n = fread(buf + len, 1, sizeof(buf) - 1 - len, fp);
        if (n == 0)
            break;
        len += n;
    }
    buf[len] = '\0';

    int status = pclose(fp);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return NULL;

    return buf;
}

Msg load_directory(const char *path, int show_hidden)
{
    static Entry loaded[MAX_ENTRIES];

    DIR *dir = opendir(path);
    if (!dir) {
        Msg msg = { .type = MSG_OP_FAILED };
        snprintf(msg.error, sizeof(msg.error), "%s: %s", path, strerror(errno));
        return msg;
    }

    int count = 0;
    struct dirent *de;
    char fullpath[PATH_MAX_LEN];

    while ((de = readdir(dir)) && count < MAX_ENTRIES) {
        if (is_protected_name(de->d_name))
            continue;
        if (!show_hidden && is_hidden_name(de->d_name))
            continue;

        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, de->d_name);
        if (lstat(fullpath, &loaded[count].st) == 0) {
            strncpy(loaded[count].name, de->d_name, NAME_MAX_LEN);
            loaded[count].name[NAME_MAX_LEN] = '\0';
            count++;
        }
    }
    closedir(dir);

    classify_git_status(read_git_status(path), loaded, count);

    Msg msg = { .type = MSG_DIR_LOADED };
    msg.dir_loaded.entries = loaded;
    msg.dir_loaded.entry_count = count;
    strncpy(msg.dir_loaded.path, path, sizeof(msg.dir_loaded.path) - 1);
    msg.dir_loaded.path[sizeof(msg.dir_loaded.path) - 1] = '\0';
    return msg;
}

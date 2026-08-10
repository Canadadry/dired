#include "trash.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static Msg msg_failed(const char *fmt, const char *detail)
{
    Msg msg = { .type = MSG_OP_FAILED };
    snprintf(msg.error, sizeof(msg.error), fmt, detail);
    return msg;
}

static int ensure_trash_dir(char *out, size_t out_size)
{
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    snprintf(out, out_size, "%s/.trash", home);
    if (mkdir(out, 0755) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

Msg trash_item(const char *path)
{
    char trash_dir[PATH_MAX_LEN];
    if (ensure_trash_dir(trash_dir, sizeof(trash_dir)) != 0)
        return msg_failed("trash: %s", strerror(errno));

    const char *slash = strrchr(path, '/');
    const char *base_name = slash ? slash + 1 : path;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    char trashed_path[PATH_MAX_LEN];
    char sidecar_path[PATH_MAX_LEN];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(trashed_path, sizeof(trashed_path), "%s/%s.%ld%09ld",
             trash_dir, base_name, (long)ts.tv_sec, ts.tv_nsec);
    snprintf(sidecar_path, sizeof(sidecar_path), "%s.trashinfo", trashed_path);
#pragma GCC diagnostic pop

    if (rename(path, trashed_path) != 0)
        return msg_failed("trash: %s", strerror(errno));

    FILE *f = fopen(sidecar_path, "w");
    if (!f)
        return msg_failed("trash: %s", strerror(errno));
    fprintf(f, "%s\n", path);
    fclose(f);

    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

#include "loaddir.h"
#include "helpers.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

Msg load_directory(const char *path)
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

        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, de->d_name);
        if (lstat(fullpath, &loaded[count].st) == 0) {
            strncpy(loaded[count].name, de->d_name, NAME_MAX_LEN);
            loaded[count].name[NAME_MAX_LEN] = '\0';
            count++;
        }
    }
    closedir(dir);

    Msg msg = { .type = MSG_DIR_LOADED };
    msg.dir_loaded.entries = loaded;
    msg.dir_loaded.entry_count = count;
    strncpy(msg.dir_loaded.path, path, sizeof(msg.dir_loaded.path) - 1);
    msg.dir_loaded.path[sizeof(msg.dir_loaded.path) - 1] = '\0';
    return msg;
}

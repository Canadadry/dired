#include "gitstatus.h"
#include <string.h>
#include <sys/stat.h>

static GitStatusTag tag_for_code(char x, char y)
{
    if (x == '?' && y == '?')
        return GIT_STATUS_UNTRACKED;
    if (x == '!' && y == '!')
        return GIT_STATUS_IGNORED;
    if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D'))
        return GIT_STATUS_CONFLICTED;
    if (x != ' ' || y != ' ')
        return GIT_STATUS_MODIFIED;
    return GIT_STATUS_NONE;
}

static int entry_matches(const Entry *e, const char *path)
{
    if (S_ISDIR(e->st.st_mode)) {
        size_t name_len = strlen(e->name);
        return strncmp(path, e->name, name_len) == 0 && path[name_len] == '/';
    }
    return strcmp(path, e->name) == 0;
}

void classify_git_status(const char *porcelain_text, Entry *entries, int entry_count)
{
    for (int i = 0; i < entry_count; i++)
        entries[i].git_status = GIT_STATUS_NONE;

    if (!porcelain_text)
        return;

    const char *p = porcelain_text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);

        if (line_len >= 3 && p[2] == ' ') {
            char x = p[0];
            char y = p[1];
            GitStatusTag tag = tag_for_code(x, y);

            size_t path_len = line_len - 3;
            char path[PATH_MAX_LEN];
            if (path_len >= sizeof(path))
                path_len = sizeof(path) - 1;
            memcpy(path, p + 3, path_len);
            path[path_len] = '\0';

            const char *arrow = strstr(path, " -> ");
            const char *effective_path = arrow ? arrow + 4 : path;

            for (int i = 0; i < entry_count; i++) {
                if (entry_matches(&entries[i], effective_path) && tag > entries[i].git_status)
                    entries[i].git_status = tag;
            }
        }

        p = nl ? nl + 1 : p + line_len;
    }
}

#include "helpers.h"
#include <string.h>
#include <stdio.h>

int is_protected_name(const char *name)
{
    return (!strcmp(name, ".") || !strcmp(name, ".."));
}

static int name_collides(const char *name, const Entry *entries, int entry_count)
{
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].name, name) == 0)
            return 1;
    }
    return 0;
}

/* Splits at the last '.', except a leading '.' (hidden files like
 * ".bashrc") is kept as part of the stem rather than treated as an
 * extension marker. */
static void split_ext(const char *name, char *stem, char *ext)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) {
        strcpy(stem, name);
        ext[0] = '\0';
        return;
    }

    size_t stem_len = (size_t)(dot - name);
    memcpy(stem, name, stem_len);
    stem[stem_len] = '\0';
    strcpy(ext, dot);
}

void find_available_name(const char *base_name, const Entry *entries, int entry_count,
                          char *out_name, size_t out_size)
{
    if (!name_collides(base_name, entries, entry_count)) {
        snprintf(out_name, out_size, "%s", base_name);
        return;
    }

    char stem[NAME_MAX_LEN + 1];
    char ext[NAME_MAX_LEN + 1];
    split_ext(base_name, stem, ext);

    for (int n = 1; ; n++) {
        snprintf(out_name, out_size, "%s (%d)%s", stem, n, ext);
        if (!name_collides(out_name, entries, entry_count))
            return;
    }
}

int is_binary_content(const unsigned char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\0')
            return 1;
    }
    return 0;
}

void mode_to_str(mode_t m, char *out)
{
    out[0] = S_ISDIR(m) ? 'd' : '-';
    out[1] = (m & S_IRUSR) ? 'r' : '-';
    out[2] = (m & S_IWUSR) ? 'w' : '-';
    out[3] = (m & S_IXUSR) ? 'x' : '-';
    out[4] = (m & S_IRGRP) ? 'r' : '-';
    out[5] = (m & S_IWGRP) ? 'w' : '-';
    out[6] = (m & S_IXGRP) ? 'x' : '-';
    out[7] = (m & S_IROTH) ? 'r' : '-';
    out[8] = (m & S_IWOTH) ? 'w' : '-';
    out[9] = (m & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

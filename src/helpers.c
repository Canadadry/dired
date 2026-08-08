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

NameKind classify_new_name(const char *raw, char *out_name, size_t out_size)
{
    size_t len = strlen(raw);
    while (len > 0 && raw[len - 1] == '/')
        len--;

    if (len == 0) {
        out_name[0] = '\0';
        return NAME_EMPTY;
    }

    NameKind kind = (raw[len] == '/') ? NAME_IS_DIR : NAME_IS_FILE;
    size_t copy_len = len < out_size - 1 ? len : out_size - 1;
    memcpy(out_name, raw, copy_len);
    out_name[copy_len] = '\0';
    return kind;
}

/* Same leading-dot convention as split_ext(): a hidden file like ".bashrc"
 * has no extension, not an extension of ".bashrc". */
static const char *entry_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name)
        return "";
    return dot;
}

int entry_compare(const Entry *a, const Entry *b, SortMode sort_mode, GroupMode group_mode)
{
    if (group_mode != GROUP_MIXED) {
        int a_is_dir = S_ISDIR(a->st.st_mode);
        int b_is_dir = S_ISDIR(b->st.st_mode);
        if (a_is_dir != b_is_dir) {
            int dirs_first = (group_mode == GROUP_DIRS_FIRST);
            if (a_is_dir)
                return dirs_first ? -1 : 1;
            return dirs_first ? 1 : -1;
        }
    }

    int descending = (sort_mode % 2) != 0;
    int cmp = 0;

    switch (sort_mode / 2) {
    case 0: /* name */
        cmp = strcmp(a->name, b->name);
        break;
    case 1: /* date */
        if (a->st.st_mtime < b->st.st_mtime) cmp = -1;
        else if (a->st.st_mtime > b->st.st_mtime) cmp = 1;
        break;
    case 2: /* size */
        if (a->st.st_size < b->st.st_size) cmp = -1;
        else if (a->st.st_size > b->st.st_size) cmp = 1;
        break;
    case 3: /* extension */
        cmp = strcmp(entry_ext(a->name), entry_ext(b->name));
        break;
    }

    if (cmp != 0)
        return descending ? -cmp : cmp;

    return strcmp(a->name, b->name);
}

void sort_entries(Entry *entries, int count, SortMode sort_mode, GroupMode group_mode)
{
    for (int i = 1; i < count; i++) {
        Entry key = entries[i];
        int j = i - 1;
        while (j >= 0 && entry_compare(&entries[j], &key, sort_mode, group_mode) > 0) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
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

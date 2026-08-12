#define _XOPEN_SOURCE 700

#include "archive.h"
#include "helpers.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *skip_spaces(const char *s)
{
    while (*s == ' ')
        s++;
    return s;
}

static const char *skip_token(const char *s)
{
    s = skip_spaces(s);
    while (*s != '\0' && *s != ' ')
        s++;
    return s;
}

static time_t parse_datetime(const char *date, const char *time_str)
{
    struct tm tm = {0};
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %s", date, time_str);
    if (!strptime(buf, "%Y-%m-%d %H:%M", &tm))
        return 0;
    return mktime(&tm);
}

static const char *read_token(const char *p, char *out, size_t out_size)
{
    p = skip_spaces(p);
    const char *end = skip_token(p);
    size_t len = (size_t)(end - p);
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return end;
}

static const char *consume_size_date_time(const char *p, long *size,
                                           char *date, size_t date_size,
                                           char *time_str, size_t time_size)
{
    p = skip_spaces(p);
    *size = strtol(p, NULL, 10);
    p = skip_token(p);
    p = read_token(p, date, date_size);
    p = read_token(p, time_str, time_size);
    return p;
}

static void set_member(ArchiveMember *m, const char *name, size_t name_len,
                        long size, const char *date, const char *time_str)
{
    int is_dir = (name_len > 0 && name[name_len - 1] == '/');
    if (is_dir)
        name_len--;
    if (name_len >= PATH_MAX_LEN)
        name_len = PATH_MAX_LEN - 1;

    memcpy(m->path, name, name_len);
    m->path[name_len] = '\0';
    m->is_dir = is_dir;
    m->size = (off_t)size;
    m->mtime = parse_datetime(date, time_str);
}

int parse_tar_listing(const char *text, ArchiveMember *out, int out_capacity)
{
    int count = 0;
    const char *line = text;

    while (*line != '\0' && count < out_capacity) {
        const char *line_end = strchr(line, '\n');
        size_t line_len = line_end ? (size_t)(line_end - line) : strlen(line);

        if (line_len == 0) {
            line = line_end ? line_end + 1 : line + line_len;
            continue;
        }

        const char *p = skip_token(line);
        p = skip_token(p);

        long size;
        char date[16];
        char time_str[16];
        p = consume_size_date_time(p, &size, date, sizeof(date), time_str, sizeof(time_str));
        p = skip_spaces(p);

        set_member(&out[count], p, line_len - (size_t)(p - line), size, date, time_str);
        count++;

        line = line_end ? line_end + 1 : line + line_len;
    }

    return count;
}

static int is_separator_line(const char *line, size_t line_len)
{
    return line_len > 0 && line[0] == '-';
}

static int is_zip_data_line(const char *line, size_t line_len)
{
    const char *p = skip_spaces(line);
    return p < line + line_len && *p >= '0' && *p <= '9';
}

int parse_zip_listing(const char *text, ArchiveMember *out, int out_capacity)
{
    int count = 0;
    const char *line = text;
    int in_data = 0;

    while (*line != '\0' && count < out_capacity) {
        const char *line_end = strchr(line, '\n');
        size_t line_len = line_end ? (size_t)(line_end - line) : strlen(line);

        if (is_separator_line(line, line_len)) {
            if (!in_data) {
                in_data = 1;
                line = line_end ? line_end + 1 : line + line_len;
                continue;
            }
            break;
        }

        if (in_data && is_zip_data_line(line, line_len)) {
            long size;
            char date[16];
            char time_str[16];
            const char *p = consume_size_date_time(line, &size, date, sizeof(date),
                                                    time_str, sizeof(time_str));
            p = skip_spaces(p);

            set_member(&out[count], p, line_len - (size_t)(p - line), size, date, time_str);
            count++;
        }

        line = line_end ? line_end + 1 : line + line_len;
    }

    return count;
}

static int find_child_index(const ArchiveMember *out, int count, const char *name, size_t name_len)
{
    for (int i = 0; i < count; i++) {
        if (strlen(out[i].path) == name_len && strncmp(out[i].path, name, name_len) == 0)
            return i;
    }
    return -1;
}

int archive_children_at(const ArchiveMember *members, int member_count,
                         const char *subfolder, int show_hidden,
                         ArchiveMember *out, int out_capacity)
{
    size_t prefix_len = strlen(subfolder);

    int count = 0;
    for (int i = 0; i < member_count; i++) {
        const char *path = members[i].path;
        size_t path_len = strlen(path);

        if (prefix_len > 0) {
            if (path_len <= prefix_len)
                continue;
            if (strncmp(path, subfolder, prefix_len) != 0)
                continue;
            if (path[prefix_len] != '/')
                continue;
        }

        const char *remainder = path + prefix_len + (prefix_len > 0 ? 1 : 0);
        if (*remainder == '\0')
            continue;

        if (!show_hidden && is_hidden_name(remainder))
            continue;

        const char *slash = strchr(remainder, '/');
        size_t name_len = slash ? (size_t)(slash - remainder) : strlen(remainder);
        int is_dir = slash != NULL ? 1 : members[i].is_dir;

        int existing = find_child_index(out, count, remainder, name_len);
        if (existing >= 0) {
            if (is_dir)
                out[existing].is_dir = 1;
            continue;
        }

        if (count >= out_capacity)
            continue;

        size_t copy_len = name_len >= PATH_MAX_LEN ? PATH_MAX_LEN - 1 : name_len;
        memcpy(out[count].path, remainder, copy_len);
        out[count].path[copy_len] = '\0';
        out[count].is_dir = is_dir;
        out[count].size = slash ? 0 : members[i].size;
        out[count].mtime = slash ? 0 : members[i].mtime;
        count++;
    }

    return count;
}

static int archive_glob_find(const ArchiveMember *out, int count, const char *candidate)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(out[i].path, candidate) == 0)
            return i;
    }
    return -1;
}

int archive_glob_matches(const ArchiveMember *members, int member_count,
                          const char *subfolder,
                          FilterType filter_type, const char *pattern,
                          ArchiveMember *out, int out_capacity, int *out_truncated)
{
    size_t prefix_len = strlen(subfolder);
    int count = 0;
    *out_truncated = 0;

    for (int i = 0; i < member_count; i++) {
        const char *path = members[i].path;
        size_t path_len = strlen(path);

        if (prefix_len > 0) {
            if (path_len <= prefix_len)
                continue;
            if (strncmp(path, subfolder, prefix_len) != 0)
                continue;
            if (path[prefix_len] != '/')
                continue;
        }

        const char *remainder = path + prefix_len + (prefix_len > 0 ? 1 : 0);
        size_t remainder_len = strlen(remainder);
        if (remainder_len == 0)
            continue;

        for (size_t pos = 1; pos <= remainder_len; pos++) {
            if (pos < remainder_len && remainder[pos] != '/')
                continue;

            int is_leaf = (pos == remainder_len);
            int is_dir = is_leaf ? members[i].is_dir : 1;

            char candidate[PATH_MAX_LEN];
            size_t clen = pos >= PATH_MAX_LEN ? PATH_MAX_LEN - 1 : pos;
            memcpy(candidate, remainder, clen);
            candidate[clen] = '\0';

            int existing = archive_glob_find(out, count, candidate);
            if (existing >= 0) {
                if (is_dir)
                    out[existing].is_dir = 1;
                continue;
            }

            if (!filter_matches(candidate, filter_type, pattern))
                continue;

            if (count >= out_capacity) {
                *out_truncated = 1;
                continue;
            }

            strncpy(out[count].path, candidate, sizeof(out[count].path) - 1);
            out[count].path[sizeof(out[count].path) - 1] = '\0';
            out[count].is_dir = is_dir;
            out[count].size = is_leaf ? members[i].size : 0;
            out[count].mtime = is_leaf ? members[i].mtime : 0;
            count++;
        }
    }

    return count;
}

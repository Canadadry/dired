#ifndef DIRED_HELPERS_H
#define DIRED_HELPERS_H

#include <stddef.h>
#include <sys/stat.h>
#include "model.h"

typedef enum {
    NAME_EMPTY = 0,
    NAME_IS_FILE,
    NAME_IS_DIR,
} NameKind;

int is_protected_name(const char *name);
void mode_to_str(mode_t m, char *out);
int is_binary_content(const unsigned char *buf, size_t len);
void find_available_name(const char *base_name, const Entry *entries, int entry_count,
                          char *out_name, size_t out_size);
NameKind classify_new_name(const char *raw, char *out_name, size_t out_size);

/* Relative order of two entries under the given sort key/direction and
 * directory-grouping mode. Returns <0, 0, >0 like strcmp/qsort. Any tie
 * (including the extension key's no-extension bucket) breaks by name,
 * ascending, regardless of the requested direction. */
int entry_compare(const Entry *a, const Entry *b, SortMode sort_mode, GroupMode group_mode);

/* Sorts entries[0..count) in place per entry_compare. */
void sort_entries(Entry *entries, int count, SortMode sort_mode, GroupMode group_mode);

#endif // DIRED_HELPERS_H

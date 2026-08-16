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

#define PREVIEW_RULE_MAX 64
#define PREVIEW_SUFFIX_MAX 64
#define PREVIEW_TEMPLATE_MAX 256

typedef struct {
    char suffix[PREVIEW_SUFFIX_MAX];
    char argv_template[PREVIEW_TEMPLATE_MAX];
} PreviewRule;

int parse_preview_rules(const char *text, PreviewRule *out_rules, int max_rules,
                         char *errbuf, size_t errbuf_len);
const PreviewRule *match_preview_rule(const char *filename, const PreviewRule *rules, int rule_count);

#define PREVIEW_ARGV_MAX 16
#define PREVIEW_EXEC_TOKEN_MAX (PATH_MAX_LEN + 64)

int build_preview_argv(const PreviewRule *rule, const char *file_path, int col_width,
                        char argv_buf[PREVIEW_ARGV_MAX][PREVIEW_EXEC_TOKEN_MAX],
                        char *out_argv[PREVIEW_ARGV_MAX + 1]);

int dired_effective_home(char *out, size_t out_size);

int is_protected_name(const char *name);
int is_hidden_name(const char *name);
ArchiveFormat archive_format_for_name(const char *name);
int filter_matches(const char *name, FilterType type, const char *pattern);
int filter_is_valid(FilterType type, const char *pattern);

void apply_filter(const Entry *unfiltered_entries, int unfiltered_count,
                   FilterType filter_type, const char *filter_pattern,
                   int empty_matches_all,
                   SortMode sort_mode, GroupMode group_mode,
                   Entry *out_entries, int out_capacity, int *out_entry_count,
                   int *out_truncated);
void dirname_of(const char *name, const char *current_path, char *out, size_t out_size);
void mode_to_str(mode_t m, char *out);
int is_binary_content(const unsigned char *buf, size_t len);
void find_available_name(const char *base_name, const Entry *entries, int entry_count,
                          char *out_name, size_t out_size);
NameKind classify_new_name(const char *raw, char *out_name, size_t out_size);

int visible_entry_rows(int term_height, int has_virtual_line);

int page_snap_offset(int selected, int entry_count, int visible_rows);

/* Relative order of two entries under the given sort key/direction and
 * directory-grouping mode. Returns <0, 0, >0 like strcmp/qsort. Any tie
 * (including the extension key's no-extension bucket) breaks by name,
 * ascending, regardless of the requested direction. */
int entry_compare(const Entry *a, const Entry *b, SortMode sort_mode, GroupMode group_mode);

/* Sorts entries[0..count) in place per entry_compare. */
void sort_entries(Entry *entries, int count, SortMode sort_mode, GroupMode group_mode);

#endif // DIRED_HELPERS_H

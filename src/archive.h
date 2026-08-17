#ifndef DIRED_ARCHIVE_H
#define DIRED_ARCHIVE_H

#include <stddef.h>
#include "model.h"

typedef enum {
    ARCHIVE_CREATE_ZIP = 0,
    ARCHIVE_CREATE_TAR,
    ARCHIVE_CREATE_TARGZ,
} ArchiveCreateFormat;

const char *archive_create_base_name(ArchiveCreateFormat format);

void archive_create_destination_name(ArchiveCreateFormat format,
                                      const Entry *entries, int entry_count,
                                      char *out_name, size_t out_size);

int archive_extract_subfolder_stem(const char *archive_name, char *out_stem, size_t out_size);

int archive_extract_destination_name(const char *archive_name,
                                      const Entry *entries, int entry_count,
                                      const char *const *claimed_names, int claimed_count,
                                      char *out_name, size_t out_size);

int parse_tar_listing(const char *text, ArchiveMember *out, int out_capacity);
int parse_zip_listing(const char *text, ArchiveMember *out, int out_capacity);

int archive_children_at(const ArchiveMember *members, int member_count,
                         const char *subfolder, int show_hidden,
                         ArchiveMember *out, int out_capacity);

int archive_glob_matches(const ArchiveMember *members, int member_count,
                          const char *subfolder,
                          FilterType filter_type, const char *pattern,
                          ArchiveMember *out, int out_capacity, int *out_truncated);

#endif

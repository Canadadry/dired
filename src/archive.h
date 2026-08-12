#ifndef DIRED_ARCHIVE_H
#define DIRED_ARCHIVE_H

#include "model.h"

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

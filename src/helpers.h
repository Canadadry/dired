#ifndef DIRED_HELPERS_H
#define DIRED_HELPERS_H

#include <stddef.h>
#include <sys/stat.h>
#include "model.h"

int is_protected_name(const char *name);
void mode_to_str(mode_t m, char *out);
int is_binary_content(const unsigned char *buf, size_t len);
void find_available_name(const char *base_name, const Entry *entries, int entry_count,
                          char *out_name, size_t out_size);

#endif // DIRED_HELPERS_H

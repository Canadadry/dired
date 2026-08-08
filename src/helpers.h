#ifndef DIRED_HELPERS_H
#define DIRED_HELPERS_H

#include <stddef.h>
#include <sys/stat.h>

int is_protected_name(const char *name);
void mode_to_str(mode_t m, char *out);
int is_binary_content(const unsigned char *buf, size_t len);

#endif // DIRED_HELPERS_H

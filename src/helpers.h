#ifndef DIRED_HELPERS_H
#define DIRED_HELPERS_H

#include <sys/stat.h>

int is_protected_name(const char *name);
void mode_to_str(mode_t m, char *out);

#endif // DIRED_HELPERS_H

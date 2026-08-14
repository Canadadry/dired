#ifndef DIRED_GITSTATUS_H
#define DIRED_GITSTATUS_H

#include "model.h"

void classify_git_status(const char *porcelain_text, const char *prefix, Entry *entries, int entry_count);

#endif

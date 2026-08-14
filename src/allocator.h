#ifndef DIRED_ALLOCATOR_H
#define DIRED_ALLOCATOR_H

#include <stddef.h>
#include <stdlib.h>

typedef struct {
    void* (*realloc_fn)(void* userdata, void* ptr, size_t size);
    void (*free_fn)(void* userdata, void* ptr);
    void* userdata;
} Allocator;

static inline void *std_realloc(void *userdata, void *ptr, size_t size)
{
    (void)userdata;
    return realloc(ptr, size);
}

static inline void std_free(void *userdata, void *ptr)
{
    (void)userdata;
    free(ptr);
}

static inline Allocator std_allocator(void)
{
    return (Allocator){ .realloc_fn = std_realloc, .free_fn = std_free, .userdata = NULL };
}

#endif

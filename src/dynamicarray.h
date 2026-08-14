#ifndef DIRED_DYNAMICARRAY_H
#define DIRED_DYNAMICARRAY_H

#include "allocator.h"

#define ARRAY(type) type##_array

#define CREATE_ARRAY_TYPE(type)                                    \
    typedef struct {                                               \
        type* data;                                                \
        int len;                                                   \
        int capacity;                                              \
        Allocator alloc;                                           \
    } ARRAY(type);                                                 \
    int array_append_##type(ARRAY(type)* a, type val);             \
    int array_reserve_##type(ARRAY(type)* a, int cap);             \
    ARRAY(type) array_create_##type(Allocator alloc);

#define WRITE_ARRAY_IMPL(type)                                              \
    int array_reserve_##type(ARRAY(type)* a, int cap) {                     \
        if (cap <= 0) return 0;                                             \
        if (a->alloc.realloc_fn == NULL) return 1;                          \
        int next_capacity = 1;                                              \
        while (a->len + cap >= next_capacity)                               \
            next_capacity = 2 * next_capacity;                              \
        if (next_capacity < a->capacity) return 0;                          \
        a->data = a->alloc.realloc_fn(                                      \
            a->alloc.userdata, a->data, next_capacity * sizeof(type));      \
        if (a->data == NULL) return 2;                                      \
        a->capacity = next_capacity;                                        \
        return 0;                                                           \
    }                                                                       \
    int array_append_##type(ARRAY(type)* a, type val) {                     \
        if (a->len >= a->capacity) {                                        \
            if (array_reserve_##type(a, 1) != 0) return 1;                  \
        }                                                                   \
        a->data[a->len] = val;                                              \
        a->len++;                                                           \
        return 0;                                                           \
    }                                                                       \
    type##_array array_create_##type(Allocator alloc) {                     \
        return (type##_array){ .alloc = alloc };                            \
    }

#endif

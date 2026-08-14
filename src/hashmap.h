#ifndef DIRED_HASHMAP_H
#define DIRED_HASHMAP_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dynamicarray.h"

typedef enum {
    UpsertActionCreate,
    UpsertActionUpdate,
    UpsertActionDelete,
} UpsertAction;

#define HASHMAP(VALUE) VALUE##HashMap
#define HASMMAP_KEY_LEN 1024

static inline unsigned long hash(const char *s)
{
    unsigned long h = 5381;
    unsigned char c;
    while ((c = (unsigned char)*s++))
        h = h * 33 + c;
    return h;
}

#define CREATE_HASHMAP(VALUE)                                                      \
typedef struct {                                                                   \
    char key[HASMMAP_KEY_LEN];                                                     \
    VALUE value;                                                                   \
    int children[4];                                                               \
} VALUE##HashMapCell;                                                              \
CREATE_ARRAY_TYPE(VALUE##HashMapCell);                                             \
typedef struct {                                                                   \
    ARRAY(VALUE##HashMapCell) data;                                                \
} VALUE##HashMap;                                                                  \
                                                                                    \
VALUE* VALUE##_upsert(VALUE##HashMap *m, const char *key, UpsertAction action);

#define WRITE_HASHMAP_IMPL(VALUE)                                                                    \
WRITE_ARRAY_IMPL(VALUE##HashMapCell);                                                                \
                                                                                                       \
static void VALUE##_delete_at(VALUE##HashMap *m, int target, int target_parent, int target_slot) {   \
    int cur = target, cur_parent = target_parent, cur_slot = target_slot;                            \
    while (1) {                                                                                      \
        VALUE##HashMapCell *cell = &m->data.data[cur];                                               \
        int child_slot = -1;                                                                         \
        for (int k = 0; k < 4; k++) { if (cell->children[k] != 0) { child_slot = k; break; } }       \
        if (child_slot == -1) break;                                                                 \
        int child_index = cell->children[child_slot];                                                \
        VALUE##HashMapCell *child = &m->data.data[child_index];                                      \
        memcpy(cell->key, child->key, HASMMAP_KEY_LEN);                                              \
        cell->value = child->value;                                                                  \
        cur_parent = cur; cur_slot = child_slot; cur = child_index;                                  \
    }                                                                                                 \
    if (cur_slot != -1) m->data.data[cur_parent].children[cur_slot] = 0;                             \
    memmove(&m->data.data[cur], &m->data.data[cur + 1],                                              \
            (size_t)(m->data.len - cur - 1) * sizeof(VALUE##HashMapCell));                           \
    m->data.len--;                                                                                    \
    for (int i = 0; i < m->data.len; i++)                                                             \
        for (int k = 0; k < 4; k++)                                                                   \
            if (m->data.data[i].children[k] > cur) m->data.data[i].children[k]--;                     \
}                                                                                                      \
                                                                                                       \
VALUE* VALUE##_upsert(VALUE##HashMap *m, const char *key, UpsertAction action) {                      \
    if (key == NULL) return NULL;                                                                     \
                                                                                                        \
    if (m->data.len == 0) {                                                                           \
        VALUE##HashMapCell root = { 0 };                                                              \
        root.key[0] = 0x01;                                                                            \
        array_append_##VALUE##HashMapCell(&m->data, root);                                            \
    }                                                                                                  \
                                                                                                        \
    uint64_t h = hash(key);                                                                            \
    int cell_index = 0, parent_index = 0, parent_slot = -1;                                            \
                                                                                                        \
    while (1) {                                                                                        \
        VALUE##HashMapCell *cell = &m->data.data[cell_index];                                          \
                                                                                                        \
        if (cell->key[0] != 0x01 && strncmp(cell->key, key, HASMMAP_KEY_LEN) == 0) {                   \
            switch (action) {                                                                          \
            case UpsertActionCreate:                                                                    \
            case UpsertActionUpdate:                                                                    \
                return &cell->value;                                                                     \
            case UpsertActionDelete:                                                                     \
                VALUE##_delete_at(m, cell_index, parent_index, parent_slot);                             \
                return NULL;                                                                             \
            }                                                                                           \
        }                                                                                               \
                                                                                                        \
        int slot = (h >> 62) & 0x03;                                                                    \
        int next_cell = cell->children[slot];                                                           \
        h <<= 2;                                                                                        \
                                                                                                        \
        if (next_cell == 0) {                                                                           \
            if (action != UpsertActionCreate) return NULL;                                              \
            VALUE##HashMapCell new_cell = { 0 };                                                        \
            strncpy(new_cell.key, key, HASMMAP_KEY_LEN - 1);                                             \
            array_append_##VALUE##HashMapCell(&m->data, new_cell);                                       \
            int new_index = m->data.len - 1;                                                             \
            m->data.data[cell_index].children[slot] = new_index;                                          \
            return &m->data.data[new_index].value;                                                        \
        }                                                                                                \
                                                                                                         \
        parent_index = cell_index; parent_slot = slot; cell_index = next_cell;                          \
    }                                                                                                    \
}

#endif

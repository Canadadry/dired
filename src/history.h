#ifndef DIRED_HISTORY_H
#define DIRED_HISTORY_H

#include <stddef.h>
#include <stdint.h>
#include "hashmap.h"

#define HISTORY_ARENA_BYTES 20480

typedef struct {
    unsigned char data[HISTORY_ARENA_BYTES];
} CommandArena;

CREATE_HASHMAP(CommandArena)

typedef CommandArenaHashMap History;

#define FOLDER_HISTORY_ARENA_BYTES 20480
#define FILE_HISTORY_ARENA_BYTES 40960

typedef struct {
    unsigned char data[FOLDER_HISTORY_ARENA_BYTES];
} FolderHistoryArena;

typedef struct {
    unsigned char data[FILE_HISTORY_ARENA_BYTES];
} FileHistoryArena;

typedef struct {
    int text_end;
    int index_top;
    int count;
    int idx0_offset;
} HistoryArenaState;

HistoryArenaState history_arena_state(const unsigned char *buf, int n);
void history_arena_reset(unsigned char *buf, int n);
int history_arena_find(const unsigned char *buf, int n, const char *cmd);
int history_arena_push(unsigned char *buf, int n, const char *cmd);
int history_arena_dedup(unsigned char *buf, int n, const char *cmd);
int history_arena_evict_oldest(unsigned char *buf, int n);
int history_arena_record(unsigned char *buf, int n, const char *cmd);
const char *history_arena_command_at(const unsigned char *buf, int n, int position_from_newest);

History history_create(void);
void history_free(History *h);
void history_record_command(History *h, const char *folder_path, const char *cmd);
const CommandArena *history_lookup(History *h, const char *folder_path);
void history_delete_folder(History *h, const char *folder_path);
int history_folder_count(const History *h);
const char *history_folder_path_at(const History *h, int index);

int history_default_path(char *out, size_t out_size);
int history_load_file(const char *path, History *out);
int history_load_default(History *out);
int history_write_header(const char *path, uint32_t occupied_count);
int history_write_folder_slot(const char *path, const char *folder_path, const CommandArena *arena);
int history_delete_folder_slot(const char *path, const char *folder_path);

int history_load_folder_history(const char *path, FolderHistoryArena *out);
int history_load_file_history(const char *path, FileHistoryArena *out);
int history_folder_history_load_default(FolderHistoryArena *out);
int history_file_history_load_default(FileHistoryArena *out);
int history_write_folder_history(const char *path, const FolderHistoryArena *arena);
int history_write_file_history(const char *path, const FileHistoryArena *arena);

void history_prune_folder_history(FolderHistoryArena *arena);
void history_prune_file_history(FileHistoryArena *arena);

#endif

#include "history.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

WRITE_HASHMAP_IMPL(CommandArena)

#define HISTORY_FILE_MAGIC 0x54534968u
#define HISTORY_FILE_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t occupied_count;
} HistoryFileHeader;

#define HISTORY_SLOT_SIZE (HASMMAP_KEY_LEN + HISTORY_ARENA_BYTES)

static uint16_t read_u16(const unsigned char *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static void write_u16(unsigned char *p, uint16_t v)
{
    memcpy(p, &v, sizeof(v));
}

static int arena_count(const unsigned char *buf, int n)
{
    return read_u16(buf + n - 2);
}

static void arena_set_count(unsigned char *buf, int n, int count)
{
    write_u16(buf + n - 2, (uint16_t)count);
}

static int arena_index_top(const unsigned char *buf, int n)
{
    return n - 2 - arena_count(buf, n) * 2;
}

static int arena_oldest_addr(int n)
{
    return n - 4;
}

static int arena_text_end(const unsigned char *buf, int n)
{
    int count = arena_count(buf, n);
    int max_end = 0;
    for (int i = 0; i < count; i++) {
        int addr = n - 4 - 2 * i;
        int offset = read_u16(buf + addr);
        int len = (int)strlen((const char *)buf + offset) + 1;
        if (offset + len > max_end)
            max_end = offset + len;
    }
    return max_end;
}

static int arena_find_addr(const unsigned char *buf, int n, const char *cmd)
{
    int count = arena_count(buf, n);
    for (int i = 0; i < count; i++) {
        int addr = n - 4 - 2 * i;
        int offset = read_u16(buf + addr);
        if (strcmp((const char *)buf + offset, cmd) == 0)
            return addr;
    }
    return -1;
}

static void arena_push_index(unsigned char *buf, int n, int text_offset)
{
    int count = arena_count(buf, n);
    int new_index_top = n - 2 - (count + 1) * 2;
    write_u16(buf + new_index_top, (uint16_t)text_offset);
    arena_set_count(buf, n, count + 1);
}

HistoryArenaState history_arena_state(const unsigned char *buf, int n)
{
    HistoryArenaState s;
    s.count = arena_count(buf, n);
    s.index_top = arena_index_top(buf, n);
    s.text_end = arena_text_end(buf, n);
    s.idx0_offset = s.count > 0 ? read_u16(buf + arena_oldest_addr(n)) : -1;
    return s;
}

void history_arena_reset(unsigned char *buf, int n)
{
    memset(buf, 0, (size_t)n);
}

int history_arena_find(const unsigned char *buf, int n, const char *cmd)
{
    int addr = arena_find_addr(buf, n, cmd);
    if (addr < 0)
        return -1;
    return read_u16(buf + addr);
}

int history_arena_push(unsigned char *buf, int n, const char *cmd)
{
    int len = (int)strlen(cmd) + 1;
    int text_end = arena_text_end(buf, n);
    int index_top = arena_index_top(buf, n);
    if (text_end + len > index_top - 2)
        return -1;
    memcpy(buf + text_end, cmd, (size_t)len);
    arena_push_index(buf, n, text_end);
    return 0;
}

int history_arena_dedup(unsigned char *buf, int n, const char *cmd)
{
    int found_addr = arena_find_addr(buf, n, cmd);
    if (found_addr < 0)
        return -1;

    int text_offset = read_u16(buf + found_addr);
    int index_top = arena_index_top(buf, n);
    int shift_len = found_addr - index_top;
    if (shift_len > 0)
        memmove(buf + index_top + 2, buf + index_top, (size_t)shift_len);

    int count = arena_count(buf, n);
    arena_set_count(buf, n, count - 1);
    arena_push_index(buf, n, text_offset);
    return 0;
}

int history_arena_evict_oldest(unsigned char *buf, int n)
{
    int count = arena_count(buf, n);
    if (count == 0)
        return -1;

    int oldest_addr = arena_oldest_addr(n);
    int removed_offset = read_u16(buf + oldest_addr);
    int index_top = arena_index_top(buf, n);
    int shift_len = oldest_addr - index_top;
    if (shift_len > 0)
        memmove(buf + index_top + 2, buf + index_top, (size_t)shift_len);
    arena_set_count(buf, n, count - 1);

    int removed_len = (int)strlen((const char *)buf + removed_offset) + 1;
    int text_end = arena_text_end(buf, n);
    int tail_len = text_end - (removed_offset + removed_len);
    if (tail_len > 0)
        memmove(buf + removed_offset, buf + removed_offset + removed_len, (size_t)tail_len);

    int new_count = count - 1;
    for (int i = 0; i < new_count; i++) {
        int addr = n - 4 - 2 * i;
        int offset = read_u16(buf + addr);
        if (offset > removed_offset)
            write_u16(buf + addr, (uint16_t)(offset - removed_len));
    }
    return 0;
}

int history_arena_record(unsigned char *buf, int n, const char *cmd)
{
    if (arena_find_addr(buf, n, cmd) >= 0)
        return history_arena_dedup(buf, n, cmd);

    int needed = (int)strlen(cmd) + 1 + 2;
    if (needed > n - 2)
        return -1;

    while (1) {
        int text_end = arena_text_end(buf, n);
        int index_top = arena_index_top(buf, n);
        if (index_top - text_end >= needed)
            break;
        if (arena_count(buf, n) == 0)
            return -1;
        history_arena_evict_oldest(buf, n);
    }
    return history_arena_push(buf, n, cmd);
}

const char *history_arena_command_at(const unsigned char *buf, int n, int position_from_newest)
{
    int count = arena_count(buf, n);
    if (position_from_newest < 0 || position_from_newest >= count)
        return NULL;

    int i = count - 1 - position_from_newest;
    int addr = n - 4 - 2 * i;
    int offset = read_u16(buf + addr);
    return (const char *)(buf + offset);
}

History history_create(void)
{
    History h;
    h.data = array_create_CommandArenaHashMapCell(std_allocator());
    return h;
}

void history_free(History *h)
{
    if (h->data.alloc.free_fn)
        h->data.alloc.free_fn(h->data.alloc.userdata, h->data.data);
    h->data.data = NULL;
    h->data.len = 0;
    h->data.capacity = 0;
}

void history_record_command(History *h, const char *folder_path, const char *cmd)
{
    CommandArena *arena = CommandArena_upsert(h, folder_path, UpsertActionCreate);
    if (!arena)
        return;
    history_arena_record(arena->data, HISTORY_ARENA_BYTES, cmd);
}

const CommandArena *history_lookup(History *h, const char *folder_path)
{
    return CommandArena_upsert(h, folder_path, UpsertActionUpdate);
}

void history_delete_folder(History *h, const char *folder_path)
{
    CommandArena_upsert(h, folder_path, UpsertActionDelete);
}

int history_folder_count(const History *h)
{
    int count = 0;
    for (int i = 0; i < h->data.len; i++) {
        if (h->data.data[i].key[0] != 0x01)
            count++;
    }
    return count;
}

const char *history_folder_path_at(const History *h, int index)
{
    int seen = 0;
    for (int i = 0; i < h->data.len; i++) {
        if (h->data.data[i].key[0] == 0x01)
            continue;
        if (seen == index)
            return h->data.data[i].key;
        seen++;
    }
    return NULL;
}

int history_default_path(char *out, size_t out_size)
{
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0')
        return -1;
    snprintf(out, out_size, "%s/.config/dired_history", home);
    return 0;
}

static int read_header(int fd, HistoryFileHeader *hdr)
{
    ssize_t n = pread(fd, hdr, sizeof(*hdr), 0);
    if (n != (ssize_t)sizeof(*hdr))
        return -1;
    if (hdr->magic != HISTORY_FILE_MAGIC || hdr->version != HISTORY_FILE_VERSION)
        return -1;
    return 0;
}

static off_t slot_offset(uint32_t slot_index)
{
    return (off_t)sizeof(HistoryFileHeader) + (off_t)slot_index * (off_t)HISTORY_SLOT_SIZE;
}

int history_load_file(const char *path, History *out)
{
    if (!path || !out)
        return -1;

    *out = history_create();

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    HistoryFileHeader hdr;
    if (read_header(fd, &hdr) != 0) {
        close(fd);
        return 0;
    }

    unsigned char key[HASMMAP_KEY_LEN];
    for (uint32_t i = 0; i < hdr.occupied_count; i++) {
        off_t off = slot_offset(i);
        ssize_t kn = pread(fd, key, sizeof(key), off);
        if (kn != (ssize_t)sizeof(key))
            break;
        key[HASMMAP_KEY_LEN - 1] = '\0';

        CommandArena *arena = CommandArena_upsert(out, (const char *)key, UpsertActionCreate);
        if (!arena)
            break;
        ssize_t vn = pread(fd, arena->data, HISTORY_ARENA_BYTES, off + HASMMAP_KEY_LEN);
        if (vn != (ssize_t)HISTORY_ARENA_BYTES)
            break;
    }

    close(fd);
    return 0;
}

int history_load_default(History *out)
{
    char path[HASMMAP_KEY_LEN + 64];
    if (history_default_path(path, sizeof(path)) != 0) {
        *out = history_create();
        return 0;
    }
    return history_load_file(path, out);
}

static int ensure_history_file(const char *path)
{
    int fd = open(path, O_RDWR);
    if (fd >= 0)
        return fd;
    if (errno != ENOENT)
        return -1;

    fd = open(path, O_RDWR | O_CREAT, 0600);
    if (fd < 0)
        return -1;
    if (fchmod(fd, 0600) != 0) {
        close(fd);
        return -1;
    }

    HistoryFileHeader hdr = {
        .magic = HISTORY_FILE_MAGIC,
        .version = HISTORY_FILE_VERSION,
        .occupied_count = 0,
    };
    if (pwrite(fd, &hdr, sizeof(hdr), 0) != (ssize_t)sizeof(hdr)) {
        close(fd);
        return -1;
    }
    return fd;
}

int history_write_header(const char *path, uint32_t occupied_count)
{
    int fd = ensure_history_file(path);
    if (fd < 0)
        return -1;

    HistoryFileHeader hdr = {
        .magic = HISTORY_FILE_MAGIC,
        .version = HISTORY_FILE_VERSION,
        .occupied_count = occupied_count,
    };
    ssize_t n = pwrite(fd, &hdr, sizeof(hdr), 0);
    close(fd);
    return n == (ssize_t)sizeof(hdr) ? 0 : -1;
}

static int find_slot(int fd, uint32_t occupied_count, const char *folder_path, uint32_t *out_index)
{
    unsigned char key[HASMMAP_KEY_LEN];
    for (uint32_t i = 0; i < occupied_count; i++) {
        off_t off = slot_offset(i);
        ssize_t kn = pread(fd, key, sizeof(key), off);
        if (kn != (ssize_t)sizeof(key))
            return -1;
        key[HASMMAP_KEY_LEN - 1] = '\0';
        if (strncmp((const char *)key, folder_path, HASMMAP_KEY_LEN) == 0) {
            *out_index = i;
            return 0;
        }
    }
    return -1;
}

int history_write_folder_slot(const char *path, const char *folder_path, const CommandArena *arena)
{
    int fd = ensure_history_file(path);
    if (fd < 0)
        return -1;

    HistoryFileHeader hdr;
    if (read_header(fd, &hdr) != 0) {
        close(fd);
        return -1;
    }

    uint32_t slot_index;
    int found = find_slot(fd, hdr.occupied_count, folder_path, &slot_index) == 0;

    if (found) {
        off_t off = slot_offset(slot_index) + HASMMAP_KEY_LEN;
        ssize_t n = pwrite(fd, arena->data, HISTORY_ARENA_BYTES, off);
        close(fd);
        return n == (ssize_t)HISTORY_ARENA_BYTES ? 0 : -1;
    }

    slot_index = hdr.occupied_count;
    off_t off = slot_offset(slot_index);

    unsigned char key_buf[HASMMAP_KEY_LEN];
    memset(key_buf, 0, sizeof(key_buf));
    strncpy((char *)key_buf, folder_path, HASMMAP_KEY_LEN - 1);

    ssize_t kn = pwrite(fd, key_buf, sizeof(key_buf), off);
    ssize_t vn = pwrite(fd, arena->data, HISTORY_ARENA_BYTES, off + HASMMAP_KEY_LEN);
    close(fd);
    if (kn != (ssize_t)sizeof(key_buf) || vn != (ssize_t)HISTORY_ARENA_BYTES)
        return -1;

    return history_write_header(path, hdr.occupied_count + 1);
}

int history_delete_folder_slot(const char *path, const char *folder_path)
{
    int fd = open(path, O_RDWR);
    if (fd < 0)
        return 0;

    HistoryFileHeader hdr;
    if (read_header(fd, &hdr) != 0) {
        close(fd);
        return 0;
    }

    uint32_t slot_index;
    if (find_slot(fd, hdr.occupied_count, folder_path, &slot_index) != 0) {
        close(fd);
        return 0;
    }

    uint32_t tail_slots = hdr.occupied_count - slot_index - 1;
    if (tail_slots > 0) {
        size_t tail_bytes = (size_t)tail_slots * HISTORY_SLOT_SIZE;
        unsigned char *buf = malloc(tail_bytes);
        if (!buf) {
            close(fd);
            return -1;
        }
        off_t src_off = slot_offset(slot_index + 1);
        off_t dst_off = slot_offset(slot_index);
        ssize_t rn = pread(fd, buf, tail_bytes, src_off);
        if (rn != (ssize_t)tail_bytes) {
            free(buf);
            close(fd);
            return -1;
        }
        ssize_t wn = pwrite(fd, buf, tail_bytes, dst_off);
        free(buf);
        if (wn != (ssize_t)tail_bytes) {
            close(fd);
            return -1;
        }
    }

    off_t new_size = slot_offset(hdr.occupied_count - 1);
    if (ftruncate(fd, new_size) != 0) {
        close(fd);
        return -1;
    }
    close(fd);

    return history_write_header(path, hdr.occupied_count - 1);
}

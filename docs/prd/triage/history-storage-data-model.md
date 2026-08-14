---
title: "Per-folder history storage: hashmap + compact arena + disk persistence"
description: "dired needs a fast, disk-persisted way to map an absolute folder path to a compact, self-limiting list of past commands, as the storage foundation for a future command-history-recall feature."
status: needs-triage
---

## Problem Statement

A follow-up feature (tracked separately, "initial history") wants dired's `:` command prompt to recall past `!`-commands per folder, persisted across restarts. That feature needs a place to durably store, for every folder a user has ever run a command in, an ordered list of that folder's recent commands — one that stays fast to look up (potentially hundreds of folders over a dired lifetime), stays small on disk (commands can carry secrets like API tokens, so the file needs tight permissions and shouldn't grow forever), and cleans itself up (a folder that no longer exists shouldn't leave its history behind forever).

This PRD is purely the storage foundation: a hashmap keyed by folder path, plus a compact per-folder command-list value type, plus disk persistence for both. It has no dependency on dired's `Model`/`Msg`/`Cmd`/`update()` machinery and doesn't wire into `MODE_RUN_CMD` — that's the follow-up PRD ("initial history").

**This PRD is fully self-contained.** A prototype of the hashmap (generic macro-instantiated, collision-resolved via a 4-ary trie) was built and explored during design discussion, but those prototype files will be **deleted before this PRD is implemented**. Nothing in this PRD assumes any file on disk to read or diff against — every structure and algorithm needed is specified below.

## Solution

A generic, macro-instantiated hashmap (any value type, keyed by a bounded-length string) backed by a growable array of cells, with collisions resolved via a 4-ary trie (each cell has up to 4 children, chosen by 2-bit chunks of a 64-bit hash of the key). Instantiated once with a value type purpose-built for command history: a fixed-size, self-compacting per-folder "arena" that stores a folder's recent commands with no nested dynamic allocation, so it round-trips to disk as flat bytes. A thin persistence layer keeps a file on disk mirroring the hashmap's occupied entries.

## User Stories

1. As a developer building the history-recall feature, I want to look up a folder's command history by absolute path in roughly constant time, so that pressing `:` in a folder with hundreds of prior folders visited doesn't feel slow.
2. As a developer, I want to push a new command onto a folder's history in O(1), so that recording a run doesn't add perceptible latency after a shell command finishes.
3. As a developer, I want re-running a command already in a folder's history to move it to most-recent instead of appearing twice, so that cycling through history doesn't show duplicates.
4. As a developer, I want a folder's history to have no fixed count cap, bounded only by a fixed byte budget per folder, so that folders with many short commands remember more than folders with a few long ones.
5. As a developer, I want the oldest command in a folder's history to be found in O(1) (no scanning), so that eviction under space pressure is cheap.
6. As a developer, I want to delete a folder's entire hashmap entry, so that a deleted folder's history doesn't linger forever.
7. As a developer, I want deletion to work on any hashmap entry, not just ones with no trie children, so that deletion isn't silently restricted based on internal hash-collision structure the caller can't observe or control.
8. As a developer, I want the hashmap's backing array to stay compact after a deletion (no dead/tombstoned cells), so that repeated add/delete cycles (e.g. visiting and later deleting many folders) don't leak memory or grow the array unboundedly.
9. As a developer, I want the first cell ever inserted into the hashmap to be a valid, correctly-reachable child of other cells, so that the hashmap's very first entry doesn't have different (broken) behavior from every other entry.
10. As a developer, I want inserting any number of keys — not just the first one — to reliably terminate and land the key in the map, so that the hashmap is usable beyond a single entry.
11. As a developer, I want hashmap keys to hold a full absolute path without truncation, so that two long, distinct folder paths can never collide into the same history entry.
12. As a user, I want my command history for a folder to persist across restarting dired, so that yesterday's `!git status` is still on Up-arrow today.
13. As a user, I want the history file to only be readable by me (not world-readable), so that a command I ran with a secret in it (an API token, a password) isn't exposed to other users on the machine.
14. As a developer, I want recording a new command to write only that folder's data to disk (not rewrite the whole file), so that running commands frequently doesn't cause disk I/O proportional to the total number of folders ever visited.
15. As a developer, I want a brand-new folder's first command to also update the on-disk hashmap size/count metadata, so that the file's header always accurately reflects how many folder entries it holds.
16. As a developer, I want deleting a folder's hashmap entry to also update the on-disk file, so that the file never re-introduces a deleted folder's history after a restart.
17. As a developer, I want the on-disk file to store only occupied hashmap slots, never unused/empty capacity, so that the file's size tracks actual data, not internal hashmap headroom.
18. As a developer, I want a missing history file, an unset `$HOME`, or a file with a bad magic number/version to be treated as "start with empty history" rather than a hard error, so that first-run and future format changes degrade gracefully instead of crashing dired.
19. As a developer, I want a versioned file header, so that a future on-disk format change can detect and reject an old-format file instead of misreading it as corrupt data.
20. As a developer, I want table-driven tests for the pure in-memory logic (hashmap insert/lookup/delete, arena push/dedup/evict), so that the trickiest logic in this PRD is verified without touching a real filesystem.
21. As a developer, I want the persistence (file read/write) layer tested against a real temporary directory, consistent with how this codebase already tests filesystem-touching code, so that the on-disk format is verified end-to-end.

## Implementation Decisions

### Baseline hashmap library (reproduced in full — no prototype file will exist at implementation time)

This is a generic, macro-instantiated hashmap: `CREATE_HASHMAP(VALUE)` declares a hashmap type and its API for a given value type; `WRITE_HASHMAP_IMPL(VALUE)` defines it. It's backed by a growable array (same macro pattern, `CREATE_ARRAY_TYPE`/`WRITE_ARRAY_IMPL`) using a pluggable allocator, and resolves collisions via a 4-ary trie: each cell has up to 4 children (array indices), chosen at each trie level by 2 bits of the key's hash.

**Allocator** (pluggable realloc/free, unchanged from the design prototype — no known issue):

```c
typedef struct {
    void* (*realloc_fn)(void* userdata, void* ptr, size_t size);
    void (*free_fn)(void* userdata, void* ptr);
    void* userdata;
} Allocator;
```

**Growable array** (unchanged from the design prototype — no known issue). `array_reserve` doubles capacity as needed via `realloc`; `array_append` grows on demand and appends:

```c
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
```

**Hashmap cell shape and hash function** (unchanged from the design prototype, except `HASMMAP_KEY_LEN` — see fix below):

```c
typedef enum {
    UpsertActionCreate,
    UpsertActionUpdate,
    UpsertActionDelete,
} UpsertAction;

unsigned long hash(const char *s);   /* multiplicative string hash, already exists, reuse as-is */

#define HASHMAP(VALUE) VALUE##HashMap
#define HASMMAP_KEY_LEN 1024   /* was 255 in the design prototype — see "Key length fix" below */

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
```

**`_upsert` — rewritten.** The design prototype's insert/lookup/delete logic had three problems, all fixed in the version below:

- **Index-0 sentinel bug**: `children[k] == 0` meant "no child," but array index `0` was also a legitimate index for whichever cell got inserted first, making that cell unreachable as anyone's child.
- **Delete was a leaky tombstone**: it zeroed `cell->key[0]` without touching `children[]`, so a later-reused cell could inherit stale trie children from an unrelated deleted key, and deleted cells were never reclaimed (the array only ever grew).
- **Suspected non-termination on the second+ insert** *(flagged concern — see below, not empirically verified against the original prototype since it was not available to test against at time of writing; the rewrite below sidesteps it by construction, but the implementer should specifically test "insert several keys that all require attaching below the root" before considering this done)*: the original loop, on finding an empty child slot during a Create, recorded the future append index into `children[slot]` but then continued the loop using the *pre-write* (still-zero) value of that slot rather than falling through to actually append — meaning, by a straightforward hand-trace, a Create that needs to attach anywhere below the root never obviously reaches the array-append code. The rewrite below returns immediately after appending instead of continuing to loop, avoiding this shape of bug entirely.

```c
#define WRITE_HASHMAP_IMPL(VALUE)                                                                    \
WRITE_ARRAY_IMPL(VALUE##HashMapCell);                                                                \
                                                                                                       \
static void VALUE##_delete_at(VALUE##HashMap *m, int target, int target_parent, int target_slot) {   \
    int cur = target, cur_parent = target_parent, cur_slot = target_slot;                            \
    while (1) {                                                                                      \
        VALUE##HashMapCell *cell = &m->data.data[cur];                                               \
        int child_slot = -1;                                                                         \
        for (int k = 0; k < 4; k++) { if (cell->children[k] != 0) { child_slot = k; break; } }       \
        if (child_slot == -1) break; /* cur is now childless: physically removable */                \
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
    /* Reserve index 0 as a permanent, never-keyed root the first time this  */                       \
    /* map is touched. Keys here are always absolute paths ('/'-prefixed);   */                       \
    /* the reserved cell's key[0] is a byte no real key can start with, so   */                       \
    /* it can never match a real lookup.                                    */                        \
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
```

**Delete algorithm in prose** (what `VALUE##_delete_at` above does): if the cell being deleted has any child, pull one child's key/value up into it, then repeat treating that child's now-stale slot as the new deletion target, recursing deeper each step. This is safe because lookup checks for a key match at every cell along a walk, not only at childless cells — promoting a descendant's key/value into an ancestor slot doesn't break that descendant's future lookups, the walk just matches one step earlier than before. Once a childless cell is reached, it's physically removed from the backing array (`memmove` everything after it down by one slot, `len--`), and every remaining cell's `children[]` values greater than the removed index are decremented by one, since everything after it just shifted down. Net effect: the array never contains holes or tombstones after a delete, and any key still present remains reachable via the normal hash-routed lookup.

**Key length fix**: `HASMMAP_KEY_LEN` is `1024` (matching dired's `PATH_MAX_LEN`, from `src/model.h`), not the design prototype's `255` — keys here are absolute folder paths, and 255 risked silent truncation and false collisions between two long, distinct paths.

### Per-folder command arena (the hashmap's value type for this use case)

A new value type, instantiated into the hashmap above via `CREATE_HASHMAP`/`WRITE_HASHMAP_IMPL`. Each hashmap entry's value is one fixed-size, self-contained memory region ("arena") holding one folder's command history — no nested dynamic allocation, so it round-trips to disk as flat bytes.

- **Size**: fixed at `N = 20480` bytes (20KB) per folder, chosen once for the whole application (needed so hashmap slots stay a uniform stride).
- **Layout**: two regions growing toward each other within the `N` bytes:
  - A **text region** starting at byte offset `0`, holding NUL-terminated command strings concatenated in write order, growing upward. A `text_end` cursor tracks the next free byte.
  - An **index region** at the high end, an array of `uint16_t` byte-offsets into the text region (one entry per stored command), growing downward from just below the trailing `count` field. An `index_top` cursor tracks the current (lowest-address) write boundary.
  - The final `uint16_t` in the region (fixed address, the last 2 bytes) is `count` — the number of commands currently stored.
  - `idx[0]` — a **fixed address** immediately below `count` — always holds the byte-offset of the oldest (least-recently-used) command's text. This never requires scanning to find; eviction always targets this fixed slot.
  - There is **no fixed cap on command count** per folder — capacity is purely however many commands' text + index entries fit in the `N`-byte budget.
- **Push** (record a new, non-duplicate command as most recent): write its NUL-terminated text at `text_end` (bump `text_end` up by its length), write a new `uint16_t` index entry at `index_top - 2` (bump `index_top` down by 2), increment `count`. O(1), no data movement.
- **Dedup** (recording a command that already exists in this folder's history): locate its existing index-array entry, then `memmove` every index entry positioned after it (toward `index_top`) left by one 2-byte slot to close the gap, decrement `count`, then push its *existing* text offset again as the newest entry (the text bytes themselves are never duplicated or rewritten — only re-referenced). This is a shift/compact operation, not a swap — an earlier O(1) swap-based design (exchange the found entry with whatever's in the newest slot) was explicitly rejected during design discussion because it can leave `idx[0]` pointing at a cell that isn't truly the least-recently-used entry after certain sequences of dedups; the shift-based approach always preserves exact chronological order among remaining entries.
- **Evict** (no room for a new command): remove `idx[0]` using the identical shift-based index-array removal as dedup, **and** compact the text region: remove that command's text bytes from wherever they physically sit (not necessarily the front — dedup reordering means the physically-oldest text and the logically-oldest entry can diverge), shift all subsequent text bytes down by the removed length, decrement `text_end` accordingly, and subtract the removed length from every remaining stored offset greater than the removed position. Repeat evicting `idx[0]` until enough room exists, then push the new command normally.

**Worked example** (byte-level trace, using a small `N=24`-byte arena for legibility — production uses `N=20480`; `uint16_t` fields, 2 bytes each):

```
Push "ls", "cd ..", "pwd":
  text[0..13) = "ls\0cd ..\0pwd\0"        text_end=13
  addr16=[9]"pwd"(newest)  addr18=[3]"cd.."(mid)  addr20=[0]"ls"(idx[0]=oldest)  addr22=[cnt=3]
  index_top=16

Dedup "cd ..": found at addr18 (value 3). Shift the one entry after it (addr16's
value 9) left into addr18's slot, decrement count, then push the found offset (3)
again as newest:
  addr16=[3]"cd.."(newest)  addr18=[9]"pwd"(mid)  addr20=[0]"ls"(idx[0]=oldest)
  index_top=16, count=3 (net: one shift + one push, chronological order preserved)

No room: run "git\0" (4 bytes, only 3 free = index_top(16) - text_end(13)).
Evict idx[0] (fixed addr20, currently "ls", offset 0, length 3):
  - shift the two more-recent entries toward it (addr18's value 9 -> addr20,
    addr16's value 3 -> addr18): index_top: 16->18, count: 3->2
  - compact text: drop "ls\0" (3 bytes), shift the rest down by 3, fix up
    offsets greater than the removed position (3) by -3:
    text[0..10) = "cd ..\0pwd\0"   addr18=[0]"cd.."(newest)  addr20=[6]"pwd"(idx[0]=oldest)
  Append "git\0": text[10..14)="git\0", text_end=14
    addr16=[10]"git"(newest)  addr18=[0]"cd.."(mid)  addr20=[6]"pwd"(idx[0]=oldest)  count=3

Final:
addr:   0                10      14              16     18     20     22   24
      [cd ..\0][pwd\0]  [git\0]  (free,2B)        [10]   [ 0]   [ 6]   [cnt=3]
       off0      off6    off10                   newest  mid   idx[0]=oldest
```

Tests for push/dedup/evict should reproduce this exact trace (with the real `N=20480`, or a smaller test-only arena size if the implementation allows the size to be a parameter for testability) and assert on `text_end`/`index_top`/`count`/`idx[0]` at each step.

### Disk persistence

- **File**: a single file (e.g. `~/.config/dired_history`), created with `0600` permissions (owner read/write only) — command text can carry secrets (API tokens, credentials passed as arguments), unlike the existing world-readable `~/.config/dired` preview config.
- **Format**: a small fixed header (magic bytes + a version number, so a future format change can be detected and rejected rather than misread as corrupt data), followed by the hashmap's capacity/occupied-count metadata, followed by a flat array of **occupied slots only** (never the hashmap's unused/empty backing-array capacity) — each written slot is one folder's absolute path (the key) plus its `N`-byte arena (the value).
- **Load**: happens once (this PRD provides the load function; the follow-up PRD decides when it's called — expected to be once at process startup). A missing file, an unset `$HOME`, or a header with a bad magic number/version is treated as "start with an empty hashmap," not an error — consistent with the existing fallback behavior in `load_preview_config` (`src/dired.c`) and `ensure_trash_dir` (`src/trash.c`).
- **Write — new/updated command**: recording a command against a folder writes **only that folder's slot** back to disk (its path + updated arena), not a full-file rewrite — so frequent command usage doesn't cost I/O proportional to total folders ever visited.
- **Write — new folder**: when a folder gets its *first* history entry (a new hashmap slot), the on-disk header's capacity/occupied-count metadata is also rewritten in addition to that folder's slot data, since the file's shape changed.
- **Write — deletion**: removing a folder's hashmap entry (see below) also removes its data from the on-disk file, keeping the file an exact mirror of the in-memory hashmap's current occupied set.
- This PRD exposes a small persistence API (load-all, write-one-folder-slot, write-header, delete-one-folder-slot) for the follow-up PRD to call — the follow-up PRD decides exactly when each is invoked (e.g. on process startup, after a command runs, after a folder delete).

### Deletion primitive

- The hashmap delete operation (above) is exposed as a general capability: given a folder's absolute path, remove its entry (and persist that removal to disk per the write-deletion rule above).
- **This PRD does not decide when deletion is triggered** — the follow-up PRD ("initial history") wires it to dired's directory-delete/trash actions and to a startup pass that checks every loaded folder path still exists on disk, removing any that don't.

## Testing Decisions

- Good tests here assert on in-memory state and, for the persistence layer, real file contents in a temp directory — never on dired's `Model`/`Msg`/`Cmd` types, which this PRD has no dependency on.
- **Hashmap core** (insert/lookup/delete via `WRITE_HASHMAP_IMPL`): table-driven tests instantiating the macro-generated hashmap with a simple test value type, covering: insert-then-lookup round trip for a *single* key, insert-then-lookup for *several* keys (explicitly exercising the flagged non-termination concern above — this is the most important new test in this PRD), the index-0 sentinel fix (first-ever-inserted key is reachable and correctly attachable as a child of later keys), delete of a childless cell, delete of a cell with one or more children (verifying every surviving key is still findable afterward and the backing array has no gap — `len` shrank by exactly one and no cell was skipped/orphaned), and repeated insert/delete cycles (verifying the array never grows unboundedly from tombstones). Prior art: `test/update_test.c`, `test/helpers_test.c` for table-driven pure-logic testing style in this codebase.
- **Per-folder command arena** (push/dedup/evict): table-driven tests reproducing the worked byte-level example above — push several commands and verify `text_end`/`index_top`/`count`/`idx[0]` state; dedup an existing command and verify no text duplication and correct reordering without disturbing unrelated entries; force an eviction (fill the byte budget) and verify the correct (oldest) entry is dropped, remaining offsets are correctly adjusted, and a subsequent push succeeds in the reclaimed space.
- **Disk persistence** (file read/write): tests against a real temporary directory, following the pattern already established in `test/trash_test.c` (`mkdtemp`-based temp dirs, real file I/O, no mocking of the filesystem) — covering: write-then-load round trip for one and multiple folders, missing-file-means-empty, corrupt-magic/version-means-empty, incremental single-folder write not disturbing other folders' on-disk data, and file permissions (`0600`) after creation.
- Hash function correctness/distribution (`hash()`) is not a focus of new tests here — it already exists and is out of scope for behavioral verification beyond what the hashmap-core tests exercise incidentally.

## Out of Scope

- **Any dired application wiring** — `Model`/`Msg`/`Cmd`/`update()`/`view()` changes, `MODE_RUN_CMD` integration, Up/Down key handling, `execute_run_cmd` recording a run, or loading this hashmap into dired's `Model` as a `void*`. All of this belongs to the follow-up "initial history" PRD, which will depend on this one.
- **Deciding when deletion is triggered** — the delete-by-path capability is built and tested here, but hooking it up to directory-delete/trash actions or a startup existence-sweep is the follow-up PRD's responsibility.
- **Any fixed per-folder command count cap** — an earlier "last 20, deduplicated" design was explicitly superseded in favor of the byte-budget-only arena described above.
- **Configurable arena size, file location, or file permissions** — all hardcoded (`N = 20480`, `~/.config/dired_history`, `0600`), consistent with this codebase's existing convention of hardcoding pager/preview-tool choices rather than exposing configuration surface.
- **Concurrent access from multiple dired processes** — no file locking is introduced; the last process to write a given folder's slot wins. (The follow-up PRD's "load once at startup" behavior means two concurrently running dired sessions won't see each other's new commands without a restart — a known, accepted limitation, not addressed here.)
- **Hash function changes** — `hash()` is reused as-is; no changes to its algorithm are part of this PRD.

## Further Notes

Depends on nothing else in `docs/prd/`. The follow-up PRD ("initial history," not yet written) depends on this one entirely — it will use the hashmap-of-arenas built here to record commands, load history at dired startup, and drive Up/Down recall in `MODE_RUN_CMD`, without needing to know anything about the arena's internal byte layout beyond the API this PRD exposes.

**Flagged concern for the implementer**: the "suspected non-termination on the second+ insert" issue described above was found by hand-tracing the design prototype's original loop, not by running it — an attempt to empirically verify it against a compiled test program was blocked by tooling/permissions during design discussion and never completed. The rewritten `_upsert` above was written to avoid that failure shape by construction (it returns immediately after appending a new cell rather than continuing to loop), but the very first test written for this PRD should specifically insert several keys that all require attaching below the root and confirm every one is independently findable afterward, before anything else in this PRD is built on top of it.

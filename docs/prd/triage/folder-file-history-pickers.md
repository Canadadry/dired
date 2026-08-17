---
title: "Folder and file history pickers"
description: "dired remembers nothing about where a user has been or what they've edited across a session, forcing them to re-navigate from scratch to a folder or file they touched recently."
status: needs-triage
---

## Problem Statement

dired already remembers, per folder, the shell commands run there via the `:` prompt (`022-history-storage-data-model.md`, `023-history-recall-ui.md`) — but that's the only memory the app has. There is no record of *which folders* a user has actually done something in, or *which files* they've opened in an editor. Getting back to a folder worked in five minutes ago, or reopening a file just edited, means re-walking the directory tree from scratch every time, even though dired already knows exactly where the user has been.

## Solution

Two new, independent, disk-persisted MRU lists, each with its own full-screen interactive picker:

- **Folder history** (key `I`): every folder in which the user previews or edits a file gets recorded (moved to most-recent if already present). This includes virtual folders inside archives. Plain navigation (entering/leaving a directory without touching a file in it) does not record anything.
- **File history** (key `i`): every real filesystem file opened in the editor gets recorded by its absolute path (moved to most-recent if already present). Archive members are never recorded — opening one for "editing" only ever edits a disposable extracted copy that's discarded afterward, never a real, persistent edit.

Both pickers open full-screen, list entries most-recent-first, support the same live plain/regex filter (`f`/`F`) as today's directory filter, and let you act on the highlighted entry exactly as you would in normal navigation: Enter/Space on a file-history entry previews or edits it; Enter on a folder-history entry drops you into that folder. Either way, once you leave the editor/preview, you land back in normal navigation inside that entry's directory — not back in the picker. `Esc` clears an active filter, then exits the picker.

Both lists are stored in the same `~/.config/dired_history` file as the existing per-folder command history, reusing its arena code (push/dedup/evict-oldest-on-overflow), and are pruned of entries whose folder/file no longer exists on disk at startup, the same way per-folder command history already prunes deleted folders.

## User Stories

1. As a user, I want every folder I preview or edit a file in to be remembered, so that I can jump back to it later without re-navigating.
2. As a user, I want plain navigation (moving into or out of a folder without touching a file) to *not* be recorded, so that folder history reflects places I actually did something, not everywhere I passed through.
3. As a user, I want folders I acted on while browsing inside an archive to be recorded too, so that frequently-inspected archive contents are just as reachable as real folders.
4. As a user, I want every file I open in the editor to be remembered by its real path, so that I can reopen it later without re-navigating.
5. As a user, I want files I "open" from inside an archive to be excluded from file history, so that history doesn't fill up with paths to disposable temp copies that no longer exist and were never really edited.
6. As a user, I want revisiting a folder (or reopening a file) already in its history to move it to the top instead of appearing twice, so that my most-used folders and files stay easy to find and the list doesn't fill up with duplicates.
7. As a user, I want both histories to survive restarting dired, so that history isn't wiped out every time I close the app.
8. As a user, I want to press `I` to open a full-screen list of my folder history, so that I can see everywhere I've recently worked.
9. As a user, I want to press `i` to open a full-screen list of my file history, so that I can see every file I've recently edited.
10. As a user, I want to move a highlighted selection up and down within either picker, so that I can browse the list.
11. As a user, I want to press `f` inside a picker to live-filter its list by a plain substring, so that I can quickly narrow a long history down to what I'm looking for.
12. As a user, I want to press `F` inside a picker to live-filter its list by a regex, so that I have the same filtering power here as I do in the normal directory view.
13. As a user, I want `Esc` to clear an active filter first, and exit the picker on a second `Esc` (mirroring today's directory-filter `Esc` behavior), so that the interaction feels consistent with the rest of the app.
14. As a user, I want Enter on a highlighted folder-history entry to drop me into that folder in normal navigation, so that I can pick up working there immediately.
15. As a user, I want Enter on a highlighted file-history entry to open it in the editor directly from the picker, so that I don't need an extra step to get from "found it" to "editing it."
16. As a user, I want Space on a highlighted file-history entry to preview it directly from the picker, so that I can peek at a file without committing to opening the editor.
17. As a user, I want to land back in normal navigation, inside the relevant file's or folder's directory, after leaving the editor or preview launched from a picker, so that I'm not stuck inside the picker after acting on an entry.
18. As a user, I want a folder or file that's been deleted since it was recorded to be dropped from its history automatically, so that old, dead entries don't clutter the list forever.
19. As a developer, I want the pruning to happen once at startup (like the existing per-folder command-history sweep), so that dired never touches the disk mid-session just to validate history entries.
20. As a developer, I want both new lists to reuse the existing arena push/dedup/evict-oldest arena code rather than reinventing MRU-with-eviction logic, so that this feature doesn't duplicate already-tested, already-working storage logic.
21. As a developer, I want the folder-history arena capped at 20KB and the file-history arena capped at 40KB, so that file history (which tends to hold longer, deeper paths) has more room without inflating folder history unnecessarily.
22. As a developer, I want both arenas stored in the same `~/.config/dired_history` file as the existing per-folder command arenas, so that users don't end up with multiple history files to reason about or lose track of.
23. As a developer, I want an existing (pre-this-feature) history file to keep loading correctly with no folder/file history yet (rather than being rejected or wiped), so that shipping this feature doesn't break anyone's existing per-folder command history.
24. As a developer, I want opening either picker to be a pure, synchronous `Model` mutation (no new async `Cmd`), since both lists are already loaded into memory at startup and require no disk I/O to display.
25. As a user, I want the `i`/`I` keys to only do something while I'm in normal navigation, consistent with how most other single-letter commands are already mode-gated.

## Implementation Decisions

- **Naming**: to avoid confusion with the existing per-folder `:`-command history (`History` type in `history.c`, unrelated feature), this PRD refers to its two new lists as **folder history** and **file history** throughout code, comments, and UI text.

- **Storage extension (`history.c`/`history.h`)**:
  - Two new fixed-size byte arenas, sized independently of `HISTORY_ARENA_BYTES` (which stays as-is for per-folder command arenas): a 20KB folder-history arena and a 40KB file-history arena. Each is a flat byte buffer operated on directly by the existing generic `history_arena_push` / `history_arena_dedup` / `history_arena_evict_oldest` / `history_arena_record` / `history_arena_command_at` functions — no new arena algorithm is needed, only new call sites and new storage for the buffers themselves.
  - Because the existing on-disk format assumes every stored value is a uniform-size slot in one array (`slot_offset()` computed from a single `HISTORY_SLOT_SIZE`), the two new arenas are **not** stored as hashmap entries. They live as two dedicated, fixed-size sections placed right after the file header, before the existing per-folder command-slot array begins.
  - The file format version is bumped (`HISTORY_FILE_VERSION` 1 → 2) so the loader can tell whether a given file has these two sections. A file written under version 1 (no folder/file history sections) still loads cleanly — both new arenas simply start empty — and the file is upgraded to version 2 in place the next time anything is written to it.
  - New load/write functions are added for these two fixed sections, parallel to (but distinct from) the existing per-folder `history_write_folder_slot`/`history_load_file` functions, since they operate on a single fixed-offset blob rather than a variable, growable, keyed array.
  - Two new startup-pruning sweeps (parallel to `sweep_deleted_history_folders`) walk each arena's entries and evict (via the existing arena removal path) any whose path no longer exists on disk — a folder-existence check (`stat` + `S_ISDIR`) for the folder-history arena, a plain-file-existence check for the file-history arena.

- **`Model` additions**: two new in-memory arenas (owned the same way the existing per-folder `History` is — loaded once at startup, held for the process lifetime), plus picker-specific state: which picker (if any) is open, the current highlighted index, and the live-filtered view of the arena's entries. The filter text/type reuse the existing filter machinery's shape (plain/regex, live incremental narrowing) but scoped to the picker's own entry list rather than the current directory's entries.

- **New `AppMode` values**: a mode for the folder-history picker and one for the file-history picker (browsing state). Filtering within a picker is a sub-state of that same picker mode, not a separate top-level mode — mirroring how directory filtering augments `MODE_NAV`'s entry list rather than being a wholly separate screen.

- **Key bindings (`dired.c`)**: `i` and `I` are new, unbound-today keys mapped to new `Msg` types that open the file-history and folder-history pickers respectively. Both are only handled from `MODE_NAV` (consistent with most other single-letter commands, e.g. `r`, `n`, `:`, `f`/`F`, `g`/`G`) — not from `MODE_SELECT`. `f`/`F` inside either picker map to the same filter `Msg` types already used for directory filtering; `update()` dispatches them differently based on which picker mode is currently active. The existing text-entry key-handling path (`Esc` → cancel, printable chars → text input, arrows unhandled) is extended to include the picker-filter sub-state.

- **Recording hook points (`update.c`)**: recording happens synchronously, at the point `update()` builds the `Cmd` for a preview or edit action — not deferred until `MSG_OP_SUCCEEDED` comes back, since `MSG_OP_SUCCEEDED` is generic across delete/copy/move/preview/edit and doesn't carry enough context to know which action just completed. This matches the existing per-folder command history's precedent of recording unconditionally, without inspecting outcome.
  - **Folder history**: recorded from both places `handle_nav()` currently builds a preview command (`CMD_PREVIEW`, `CMD_PREVIEW_ARCHIVE_MEMBER`) and both places it currently builds an edit command (`CMD_LAUNCH_EDITOR`, `CMD_OPEN_ARCHIVE_MEMBER`). In every case, the folder recorded is simply `out_model->current_path` at that point — which is already correct for both real and archive-virtual folders, since archive navigation already keeps `current_path` set to the virtual archive-internal path.
  - **File history**: recorded only from the real-filesystem `CMD_LAUNCH_EDITOR` branch, using the same absolute path just written into `out_cmd->path`. The archive-member edit branch (`CMD_OPEN_ARCHIVE_MEMBER`) is deliberately excluded.
  - Picking an entry from either picker (Enter/Space) re-enters this exact same dispatch path with the picked path, so acting on a history entry re-records it too — which is also how re-visiting an entry naturally moves it back to most-recent without any special-cased "re-record" logic.

- **Backward compatibility**: loading a version-1 history file must not fail or discard the existing per-folder command data; it's treated as "folder/file history not present yet" and both new arenas start empty.

## Implementation Chunks

1. **Storage layer** (`src/history.c`/`src/history.h`): add the two
   fixed-size arenas (20KB folder-history, 40KB file-history) as dedicated
   sections after the file header, bump `HISTORY_FILE_VERSION` 1 → 2 with
   backward-compatible loading of version-1 files, add the parallel
   load/write functions for the two fixed sections, and add the two
   startup-pruning sweeps (folder-existence via `stat`+`S_ISDIR`,
   plain-file-existence). Tests per `history_test.c`'s existing style:
   arena push/dedup/evict-oldest at the new sizes, versioned round-trip
   (including loading a version-1 file with both new arenas empty and
   existing per-folder data intact), and startup-pruning eviction.
   Independent of chunks 2-4 (pure storage-layer addition, no `Model`/`update()`
   wiring yet).
2. **Picker open/browse/filter/exit state machine** (`src/model.h`'s
   `AppMode` + new picker state fields, `src/update.c`, `src/dired.c` key
   bindings): add the two new `AppMode` values, the `Model` fields for
   which picker is open/highlighted index/live-filtered view, the `i`/`I`
   key bindings (`MODE_NAV`-only) that open each picker pre-populated
   from the in-memory arenas added in chunk 1, move-selection (clamped,
   no wraparound), `f`/`F` live filter (plain/regex) reusing the existing
   filter machinery's shape, and two-stage `Esc` (clear filter, then
   exit to `MODE_NAV`). Tests per `update_test.c`'s table-driven style:
   opening each picker from `MODE_NAV` vs. any other mode, move-selection
   clamping, filter narrowing (plain and regex) on a small fixture list,
   and `Esc` two-stage behavior. Depends on chunk 1 for the arena types/API
   the picker state is built from; independent of chunks 3-4 otherwise.
3. **Recording hooks and entry activation** (`src/update.c`): wire folder
   history recording into both `handle_nav()` preview-command build sites
   (`CMD_PREVIEW`, `CMD_PREVIEW_ARCHIVE_MEMBER`) and both edit-command
   build sites (`CMD_LAUNCH_EDITOR`, `CMD_OPEN_ARCHIVE_MEMBER`), and file
   history recording into the real-filesystem `CMD_LAUNCH_EDITOR` branch
   only; wire Enter/Space on a picker entry to re-enter this same
   dispatch path (so activation both performs the action and re-records
   the entry as most-recent) and land back in `MODE_NAV` in the acted-on
   entry's directory after leaving the editor/preview. Tests per
   `update_test.c`'s style: dispatching a preview/edit action (real
   folder, real file, archive-virtual folder) produces the expected arena
   mutation on a fixture arena; activating a folder-history entry
   produces the same `Cmd`/`Model` transition as normal navigation into
   that folder; activating/previewing a file-history entry produces the
   same `Cmd` as normal activation/preview from the directory listing.
   Depends on chunks 1-2 (arenas to record into, picker state to activate
   from).
4. **Picker rendering** (`src/view.c`): render both picker screens
   (most-recent-first list, highlighted selection, active filter
   indicator) mirroring the existing directory-filter view. Per the PRD's
   Testing Decisions, `view.c` rendering has no dedicated unit tests
   (consistent with this codebase's absence of view-layer snapshot
   testing) — validate manually: open each picker, filter, select an
   entry, confirm the resulting navigation/preview/edit and post-action
   landing spot. Depends on chunks 2-3 for the state it renders and the
   activation behavior it triggers.

## Testing Decisions

- Good tests here assert on `update()`'s returned `Model` or on the storage layer's exposed API directly — never on terminal output, real disk paths outside a test's own temp fixtures, or by shelling out to a real editor, consistent with this codebase's established testing conventions (`test/update_test.c`, `test/history_test.c`).
- **`history.c` storage extension** (in-scope for dedicated tests, per `history_test.c`'s existing style):
  - Folder-history and file-history arena behavior via the existing generic arena functions applied to the new, differently-sized buffers (push/dedup/evict-oldest at each arena's own capacity) — largely covered already by the existing generic arena tests, but exercised here at the 20KB/40KB sizes actually used.
  - Versioned file read/write round-trip: writing both new sections and reading them back; loading a version-1 file (no new sections) and confirming both new arenas come back empty with the existing per-folder command data intact.
  - Startup pruning: a fixture arena containing a mix of existing and nonexistent paths, asserting only the nonexistent ones are evicted.
- **`update.c` picker state machine** (in-scope for dedicated tests, per `update_test.c`'s existing table-driven style):
  - Opening each picker from `MODE_NAV` populates its entry list and highlights the most-recent entry first; opening from any other mode is a no-op.
  - Move-selection messages within a picker, clamped at both ends (no wraparound), mirroring existing list-navigation test coverage.
  - Filter messages within a picker live-narrow its entry list by plain substring and by regex, using a small in-memory fixture list rather than the real on-disk arena.
  - `Esc` behavior: clears an active picker filter on first press, exits the picker (returning to `MODE_NAV`) on a second press.
  - Activating a folder-history entry produces the same `Cmd`/`Model` transition as normally navigating into that folder; activating/previewing a file-history entry produces the same `Cmd` as normally activating/previewing that file from the directory listing.
  - Recording: dispatching a preview or edit action (real folder, real file, and archive-virtual folder) results in the expected arena mutation — asserted via the storage layer's exposed API on the fixture arena, not via disk I/O.
- **`view.c` rendering** of the two picker screens and **the real editor fork/exec path invoked from a picker** are out of scope for unit tests, consistent with the existing, already-established exclusion of `execute_launch_editor`'s fork/exec/wait behavior and the general absence of view-layer snapshot testing in this codebase. Validated manually instead: opening each picker, filtering, selecting an entry, and confirming the resulting navigation/preview/edit and post-action landing spot.

## Out of Scope

- **Any interaction with the existing per-folder `:`-command history** (`022`/`023`) beyond sharing the same on-disk file and arena code — its data, hashmap, and recall UI are untouched.
- **Manual deletion of individual history entries** from either picker (e.g. a "forget this folder/file" key) — not requested; entries only leave via MRU eviction under space pressure or startup pruning of dead paths.
- **Cross-session/multi-process consistency** — a second concurrently running dired session won't see entries recorded by the first until it restarts, matching the existing per-folder command history's accepted limitation.
- **Recording archive-member "edits" in file history** — explicitly excluded; only real filesystem files count.
- **Reloading either history mid-session** (e.g. a manual refresh key) — both load once at startup, same as the existing per-folder command history.
- **A count-based cap** ("remember the last N entries") — capacity is governed purely by each arena's fixed byte budget, matching the existing per-folder command history's model.

## Further Notes

Depends on `022-history-storage-data-model.md` (the hashmap/arena/persistence foundation this feature extends) and follows the same file (`~/.config/dired_history`). Not dependent on `023-history-recall-ui.md`, which is a different, narrower feature (Up/Down recall in the `:` prompt) that this PRD doesn't touch.

---
title: "Persist folder/file history on record"
description: "Folder history (I) and file history (i) entries are recorded in memory but never written to disk, so both lists are always empty on the next run."
status: done
---

## Problem Statement

A user previews or edits files during a `dired` session, which should
record entries into the folder-history (`I`) and file-history (`i`)
lists per `docs/prd/035-folder-file-history-pickers.md`. After quitting
and relaunching `dired`, both pickers are empty — the recorded entries
did not survive the restart, defeating the entire purpose of the
feature (PRD 035 user story 7: "I want both histories to survive
restarting dired").

## Solution

Every time a folder-history or file-history entry is recorded during a
session, the updated arena must also be flushed to the on-disk history
file (`~/.config/dired_history`), the same way the existing per-folder
`:` command history already persists synchronously on every record via
`record_run_cmd()`.

## User Stories

1. As a user, I want a folder I preview or edit a file in to still be
   in my folder history (`I`) after I restart `dired`, so that history
   actually spans sessions as promised.
2. As a user, I want a file I open in the editor to still be in my
   file history (`i`) after I restart `dired`, so that I don't lose
   recently-edited files on every quit.
3. As a user, I want re-visiting an already-recorded folder/file to
   persist its move-to-most-recent too, so the on-disk order matches
   what I'd see in the picker within the same session.
4. As a developer, I want the disk write to happen in the same place
   as other disk I/O (the impure shell in `dired.c`), not inside the
   pure `update()` core in `update.c`, so the existing pure/impure
   split introduced by the termbox2 refactor is preserved.
5. As a developer, I want the fix to reuse the existing
   `history_write_folder_history()` / `history_write_file_history()`
   functions (already used for startup-pruning writes), so no new
   serialization logic is introduced.

## Implementation Decisions

- Root cause (already diagnosed): `record_folder_history()` and
  `record_file_history()` mutate the in-memory arena via
  `history_arena_record()` but never call
  `history_write_folder_history()` / `history_write_file_history()`.
  Those two write functions are currently only invoked from the
  startup pruning path, and only when pruning actually removed an
  entry — never in response to a new record during the session. There
  is also no save-on-quit path.
- The pure `update()` core (`update.c`) has no access to the on-disk
  history path (`g_history_path` is a `dired.c`-local static) and
  should not gain direct disk I/O — that would break the pure-core /
  impure-shell split established by the termbox2 refactor. The disk
  write belongs in `dired.c`, alongside the command dispatch that
  currently invokes the preview/edit `Cmd`s that trigger folder/file
  history recording (paralleling how `record_run_cmd()` in `dired.c`
  already writes immediately after recording).
  - This should not go through the record functions in Model directly. History arena for both file and folder are global variable in dired.c, so we can write them to file after every update call. If the write is too costly, only write if the the folder_history model pointer's content or the file_history model pointer's content has changed 
- Every code path that currently leads to `record_folder_history()` or
  `record_file_history()` being called (preview, preview-archive-member,
  launch-editor, open-archive-member, and picker-driven re-invocations
  of the same dispatch) must result in the corresponding arena being
  persisted before the next event loop iteration blocks on input.
- No new on-disk format, arena sizing, or pruning behavior changes —
  this is purely about making the existing write functions fire on
  the existing record path.

## Testing Decisions

- Prior art: `test/history_test.c` already covers
  `history_write_folder_history()` / `history_write_file_history()`
  round-tripping at the storage layer — no changes needed there.
- Add/extend integration coverage (per
  `test/integration/`'s existing style, see PRD
  030/032/033) that: performs a preview or edit action that should
  record folder/file history, then verifies the on-disk history file
  reflects the new entry without requiring the process to exit
  cleanly first (i.e. read the file from a second, independent
  process/handle while the first is conceptually still "running", or
  restart the harness and confirm the entry is present).
- Only test external behavior (on-disk file contents / picker
  contents after simulated restart), not which internal function was
  called.

## Out of Scope

- Changing the on-disk format, version, or arena sizes.
- Changing pruning behavior.
- Adding a save-on-quit/atexit mechanism as a substitute — the fix
  should make every record durable immediately, matching the existing
  per-folder command history's synchronous-write behavior, not defer
  persistence to shutdown.

## Further Notes

Full investigation and file/line evidence: `docs/bugs/001-history-persistence-and-pinned-last-row.md`, "Bug A".

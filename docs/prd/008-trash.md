---
title: "Trash (~/.trash)"
description: "Deleting a file today is permanent and immediate — a single mistaken confirmation destroys data with no recovery path."
status: done
---

## Problem Statement

The existing delete action (confirmed via the foundation PRD's explicit
`MODE_CONFIRM_DELETE` state) permanently removes a file or directory with no
way to recover it if the deletion was a mistake.

## Solution

Add a trash mechanism: the existing delete keybinding moves the entry to
`~/.trash` instead of permanently deleting it, while a separate, distinct
keybinding still performs permanent deletion. Every trashed item gets a
sidecar metadata file recording its original location, laying the
groundwork for a future restore feature (`09-trash-restore`), which is
explicitly out of scope here.

## User Stories

1. As a user, I want deleting a file to move it to a trash location instead of destroying it immediately, so that I can recover from a mistake.
2. As a user, I want a separate action for permanent deletion, so that I can still bypass the trash when I actually want data gone immediately.
3. As a user, I want a trashed item that collides in name with something already in the trash to not overwrite it, so that nothing already in the trash is lost.
4. As a developer, I want each trashed item's original path recorded, so that a future restore feature can put it back.

## Implementation Decisions

**Decided:**
- The existing delete keybinding (Backspace, per current `dired.c`) moves the entry to `~/.trash` — it does **not** replace permanent delete, both remain available.
- `x` is the second, distinct keybinding for permanent deletion (bypasses the trash).
- Every trashed item always receives a uniquifying suffix on its name in `~/.trash` — not only when a collision is detected, but unconditionally. The suffix is a nanosecond-resolution timestamp (`clock_gettime(CLOCK_REALTIME)`), so trash entries never collide in practice.
- A sidecar metadata file is written alongside each trashed item: `<trashed-name>.trashinfo`, a plain-text file whose content is just the original absolute path, to support the future restore PRD (`09-trash-restore`).
- Trashing a directory is implemented as a `rename()` into `~/.trash`, which works regardless of whether the directory is empty — unlike the current `rmdir()`-based permanent delete, which requires an empty directory.
- Permanent delete (`x`) was upgraded alongside this PRD to support non-empty directories too, via a recursive `rm -rf` instead of `rmdir()`.
- `~/.trash` is created (`mkdir`, tolerating `EEXIST`) unconditionally on every trash operation, not just lazily on first use.
- The confirm-delete prompt's wording differs: "Move 'x' to trash? [y/N]" for the trash flow, "Delete 'x' ? [y/N]" (unchanged) for permanent delete. A new `Model.confirm_permanent_delete` flag (set alongside `MODE_CONFIRM_DELETE`, mirroring the existing `yank_is_move` pattern) tracks which flow is pending.
- Cross-filesystem trashing (`rename()` failing with `EXDEV` when `~/.trash` is on a different mount than the item being trashed, e.g. deleting something from a removable drive) is explicitly out of scope for this PRD — no copy+delete fallback is implemented; this case errors.
- Retention/size limits on `~/.trash` are out of scope (not implemented).

## Testing Decisions

- Which effect to issue (trash vs permanent delete, based on which key was pressed) is a table-driven `update()` test target (`test/update_test.c`), per the foundation PRD's conventions.
- The prompt-wording split is a table-driven `view()` test target (`test/view_test.c`).
- The actual `rename()`/metadata-file-write effect is I/O: pulled out of `dired.c` into a new testable module (`src/trash.c`/`trash.h`, mirroring `loaddir.c`), tested via the tier-3 style (real temp directories via `mkdtemp`, `$HOME` pointed at a temp dir) in `test/trash_test.c` — covers trashing a file, trashing a non-empty directory, and two same-named items trashed from different source directories not colliding.

## Out of Scope

- Restoring from trash (its own PRD: `09-trash-restore`).
- The `EXDEV` cross-filesystem case (documented above as an explicit gap, no fallback implemented).
- Trash retention limits, automatic emptying, or a trash-browsing UI (the last of these is part of `09-trash-restore`).

## Further Notes

Depends on `01-foundation`. Sequenced after preview and copy/move — the user explicitly judged trash lower priority ("sans importance") relative to those, despite it being one of the more fleshed-out PRDs in this roadmap discussion-wise.

---
title: "Trash (~/.trash)"
description: "Deleting a file today is permanent and immediate — a single mistaken confirmation destroys data with no recovery path."
status: needs-triage
---

> **Draft status:** written from the high-level architecture/roadmap grilling session only. This PRD has NOT had its own dedicated grilling pass — more of its shape was discussed than most other PRDs in this roadmap, but several concrete details below are still open. Needs a focused grilling session before it's implementation-ready.

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
- A second, distinct keybinding triggers permanent deletion (bypasses the trash). The exact key is not decided and is intentionally left open — the user explicitly said it's easy to change later and shouldn't be bikeshedded in this PRD.
- Every trashed item always receives a uniquifying suffix on its name in `~/.trash` — not only when a collision is detected, but unconditionally, so trash entries never collide.
- A sidecar metadata file is written alongside each trashed item, recording its original path, to support the future restore PRD (`09-trash-restore`).
- Trashing a directory is implemented as a `rename()` into `~/.trash`, which works regardless of whether the directory is empty — unlike the current `rmdir()`-based permanent delete, which requires an empty directory.
- Cross-filesystem trashing (`rename()` failing with `EXDEV` when `~/.trash` is on a different mount than the item being trashed, e.g. deleting something from a removable drive) is explicitly out of scope for this PRD — no copy+delete fallback is implemented; this case can error.

**Not yet decided — needs dedicated grilling:**
- Exact suffix format (timestamp? incrementing counter? random?).
- Exact metadata file format and naming convention (one sidecar file per trashed item vs. a single index file for the whole trash directory — the user's phrasing suggested per-item metadata but this wasn't pinned down).
- Whether `~/.trash` is created automatically if it doesn't exist, and what happens if it can't be created/written to (permissions).
- Whether the confirm-delete prompt's wording/flow differs between "move to trash" and "permanent delete", or whether permanent delete keeps the existing `MODE_CONFIRM_DELETE` flow unchanged and trash reuses it too.
- Retention/size limits on `~/.trash` (none discussed — presumed out of scope but not explicitly confirmed).

## Testing Decisions

- Not yet decided in detail. The decision of *which* effect to issue (trash vs permanent delete, based on which key was pressed) is a table-driven `update()` test target per the foundation PRD's conventions. The actual `rename()`/metadata-file-write effect is I/O, tested via the tier-3 style (real temp directories via `mkdtemp`) already used for `load_directory`, not unit tests.

## Out of Scope

- Restoring from trash (its own PRD: `09-trash-restore`).
- The `EXDEV` cross-filesystem case (documented above as an explicit gap, no fallback implemented).
- Trash retention limits, automatic emptying, or a trash-browsing UI (the last of these is part of `09-trash-restore`).

## Further Notes

Depends on `01-foundation`. Sequenced after preview and copy/move — the user explicitly judged trash lower priority ("sans importance") relative to those, despite it being one of the more fleshed-out PRDs in this roadmap discussion-wise.

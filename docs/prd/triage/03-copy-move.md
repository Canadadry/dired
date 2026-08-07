---
title: "Copy / move files and directories"
description: "dired has no way to copy or move a file or directory today — the only way to relocate something is to leave the app."
status: needs-triage
---

> **Draft status:** written from the high-level architecture/roadmap grilling session only. This PRD has NOT had its own dedicated grilling pass. Needs a focused grilling session before it's implementation-ready.

## Problem Statement

The current feature set covers navigate, open (vim), rename, create
file/directory, and delete — but not copy or move. This is the largest
functional gap in dired relative to a basic file manager.

## Solution

Add copy and move actions for the currently-selected entry, implemented as
synchronous effects within the `Cmd` mechanism established in the
foundation PRD — no queue, no async, no progress bar. Those are deliberately
deferred to the async-action-queue PRD, which later upgrades this PRD's
`Cmd`s to run through the queue instead.

## User Stories

1. As a user, I want to copy the selected file to another location, so that I can duplicate it without leaving dired.
2. As a user, I want to move the selected file or directory to another location, so that I can reorganize my filesystem.
3. As a user, I want to be warned before a copy/move overwrites an existing file, so that I don't lose data by accident.
4. As a developer, I want copy/move expressed as a `Cmd` executed synchronously by `main()`, matching how rename/delete already work, so that no new execution mechanism is needed for this PRD.

## Implementation Decisions

**Decided:**
- Copy and move ship as synchronous `Cmd` variants in the foundation's v1 `Cmd` shape (same execution model as rename/delete): `main()` runs the effect immediately and feeds the result back into `update()` as a `Msg`.
- No dependency on the async action queue (item 9) — that PRD later upgrades these operations to run through the queue; this PRD's implementation stays blocking.

**Not yet decided — needs dedicated grilling:**
- UX flow: how is the destination path chosen (typed in, like rename's inline edit buffer? a target picked by navigating to it first, "yank" then "paste"?).
- Keybindings for copy vs move vs paste.
- Recursive directory copy: does copying a directory copy its full contents? How are permission errors mid-copy handled?
- Collision/overwrite behavior beyond "warn" — overwrite, skip, rename automatically?
- Cross-filesystem behavior: `rename()` fails with `EXDEV` when source and destination are on different mounts, which is common for move. Does this PRD fall back to copy+delete for move, or surface an error (mirrors the same open question flagged as out-of-scope for trash in item 7)?
- Whether a large file copy (blocking, synchronous) is acceptable UX for v1, or whether this PRD should actually wait on the async queue after all — worth re-confirming given the queue is now sequenced very late (item 9).

## Testing Decisions

- Not yet decided in detail. Whatever `update()` logic decides *what* copy/move `Cmd` to issue (given a `Msg` and `Model`) is a table-driven test target; the actual `cp`-equivalent file-copying logic that `main()` executes is I/O and follows the same "not unit tested, or tested via tier-3 style temp-dir integration tests" pattern already used for `load_directory`.

## Out of Scope

- Async/queued execution, progress bar (item 9).
- Multi-file copy/move (item 10, multi-select).

## Further Notes

Depends on `01-foundation`. Sequenced third because the file-preview PRD was judged higher value by the user, even though copy/move fills a larger functional gap.

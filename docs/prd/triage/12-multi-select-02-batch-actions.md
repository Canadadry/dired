---
title: "Multi-select: batch trash/delete/yank/paste (chunk 2/4)"
description: "With MODE_SELECT and marking in place (chunk 1/4), the existing single-entry trash/delete/yank/paste handlers need to act on the whole marked set when one exists."
status: needs-triage
---

## Problem Statement

Chunk 1/4 adds `MODE_SELECT` and the ability to mark multiple entries, but marking anything is still inert — trash, permanent delete, yank-copy, yank-move, and paste all still act on exactly one entry (the cursor's), ignoring any marked set. Deleting, copying, or moving a batch of files still requires repeating the single-entry action once per file.

## Solution

Extend the existing trash/delete/yank-copy/yank-move/paste `Msg` handlers to check for an active marked set (`mode == MODE_SELECT` plus a non-empty marked set) and, when present, operate on every marked entry instead of just the cursor's entry — reusing the same keys and handlers rather than introducing separate batch-specific `Msg` variants.

## User Stories

1. As a user, I want trash (Backspace) and permanent delete (`x`) to act on all marked entries when I have marks, using the same keys I already use for single-entry delete, so that I don't have to learn new keybindings for the batch case.
2. As a user, I want yank-copy (`c`) and yank-move (`m`) to act on all marked entries when I have marks, and `p` to paste all of them, so that batch copy/move reuses the exact same flow as single-entry copy/move.
3. As a user, I want one combined confirmation prompt ("Delete N items?") for a batch delete/trash, instead of one prompt per file, so that clearing out many marked files doesn't require answering the same question repeatedly.
4. As a user, I want the status line to show what I've yanked as a count and operation ("Yanked: 3 items (copy)") when it's a batch, so that I have the same at-a-glance feedback I get for a single yank.
5. As a user, when multiple entries are marked, single-entry actions like rename never have to guess what to do with the rest, because those actions are only reachable outside selection mode, where no marks exist.
6. As a user, I want a batch action triggered without marking anything new in the directory I'm currently looking at to still apply to my previous (now off-screen) directory's marks, so that browsing away to double check something doesn't cancel my pending selection.
7. As a developer, I want the batch action's actual file-operation execution to defer entirely to the async action queue (`10`/`11-async-action-queue`), so that this PRD does not have to re-solve running N slow operations without blocking the UI.

## Implementation Decisions

- Trash (Backspace), permanent delete (`x`), yank-copy (`c`), and yank-move (`m`) reuse their existing keys and `Msg` handlers; scope (single entry vs. every marked entry) is decided by `mode == MODE_SELECT` plus a non-empty marked set, branching inside the existing handlers rather than introducing separate batch-specific `Msg` variants.
- A batch action triggered without first marking anything new in the currently-viewed directory (i.e., marks still belong to a previously-visited, now off-screen directory, per chunk 1/4's scoping rule) targets that off-screen directory's marks.
- Because marks only exist transiently inside `MODE_SELECT`, single-entry actions reachable only from `MODE_NAV` (rename, single delete/trash) never have to special-case "what if several are marked" — that state is unreachable from where they run.
- A batch trash/delete shows exactly one combined confirmation ("Delete N items? [y/N]") instead of one prompt per file.
- Yank state generalizes from a single pending path to an array of paths with a count, so paste (and the status-line yank display) has one code path that covers both the single-file case (count 1) and the batch case (count N) — the status line reads "Yanked: N items (copy|move)" for a batch.
- Leaving `MODE_SELECT` because a batch action ran discards marks, as part of the same transition back to `MODE_NAV` established in chunk 1/4.
- Actually dispatching a batch of N filesystem operations without blocking the UI is explicitly the async action queue's concern (`10`/`11-async-action-queue`), not solved here: reuse whichever synchronous/`Cmd`-based execution mechanism the existing single-entry trash/delete/copy/move already uses today, just looped/scoped over the marked set.

## Testing Decisions

- Good tests here assert on `update()`'s return values given specific inputs, matching the existing convention (`001-foundation`) — never on terminal output or real filesystem state.
- Table-driven cases for the batch-vs-single scope branch on trash/delete/yank handlers (marked set present vs. absent, and the off-screen-directory-marks case), the combined "Delete N items?" confirmation prompt, and the yank-state generalization (single count-1 path vs. batch count-N array) feeding paste and the status-line display.
- The actual batch filesystem `Cmd` execution (fork/exec mechanics) is out of scope for this PRD's tests, same as it's out of scope for the design — it's the async action queue's testing surface once that PRD is built.

## Out of Scope

- The async action queue mechanism itself (`10`/`11-async-action-queue`).
- The specific single-entry operations' semantics (`003-copy-move`, `008-trash`) — this PRD only adds a batch scope on top of them.
- Any rendering (chunk 3/4) or title/status-line mode indicator, help text (chunk 4/4) beyond the yank-count status line described above.

## Further Notes

Chunk 2 of 4 in the multi-select feature (originally `docs/prd/triage/12-multi-select.md`). Depends on chunk 1/4 (`MODE_SELECT`, the marked set, and its directory-scoping rules) already existing. Conceptually paired with `10`/`11-async-action-queue`, which is this PRD's main dependency for actually executing a batch without blocking — sequenced after it deliberately in the roadmap, once there's a real need for it; this chunk only wires the scope decision, not the async execution.

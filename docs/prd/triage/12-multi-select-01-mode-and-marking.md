---
title: "Multi-select: selection mode, marking primitives, scope & persistence (chunk 1/4)"
description: "No way exists to mark more than one entry at a time. Add MODE_SELECT and the pure state machine for entering/leaving it, marking entries, and scoping marks to a directory — no rendering, no batch actions yet."
status: needs-triage
---

## Problem Statement

`selected` in the current `Model` is a single index — there's no way to mark several entries at once. Before any batch action or rendering can exist, the app needs a mode to enter, primitives to mark/unmark entries while in it, and rules for how a marked set relates to the directory it was built in as the user navigates around.

## Solution

Add `MODE_SELECT` to `AppMode`. `v` toggles `MODE_NAV` <-> `MODE_SELECT`. While in `MODE_SELECT`, `space` toggles a mark on the cursor's entry, `r` performs anchor-then-extend range marking, and `t` toggles select-all/select-none. The marked set is stored alongside which directory it belongs to, survives navigation, and is discarded/rebuilt the moment a mark is placed in a different directory. This chunk is pure `update()`/`Model` state — no `view()` rendering and no batch action wiring yet (chunks 2-4).

## User Stories

1. As a user, I want to enter a selection mode with `v`, so that I can start marking multiple entries without changing what my normal navigation keys do.
2. As a user, I want `v` to only switch modes (not mark the entry under my cursor), so that entering selection mode never surprises me with an already-marked entry.
3. As a user, I want to leave selection mode with `v` or `Esc`, so that I have two familiar ways to bail out.
4. As a user, I want leaving selection mode without running a batch action to discard my marks, so that stale marks never leak into normal navigation.
5. As a user, I want `space` to toggle the mark on the entry under my cursor while in selection mode, so that I can mark files one at a time as I navigate.
6. As a user, I want a range-select gesture (`r`), so that I don't have to press `space` on every single row to mark a long contiguous run of files.
7. As a user, I want the range gesture's effect (mark vs. unmark) to be decided by the anchor entry's own state before I started the range, so that starting a range from an unmarked file extends marking and starting from an already-marked file extends unmarking.
8. As a user, I want `space` to still work independently while a range is active, so that I can hand-correct one entry without disrupting the sweep.
9. As a user, I want a second `r` press to stop the range extension (staying in selection mode), so that I can start a fresh gesture afterward.
10. As a user, I want a select-all/select-none key (`t`), so that I can quickly mark everything in the current directory or clear everything in one keystroke.
11. As a user, I want my marks to survive navigating into a subdirectory or up to the parent while still in selection mode, so that browsing around doesn't silently wipe my work.
12. As a user, I want my marks to be understood as belonging to one specific directory, so that marking a file in a different directory doesn't try to combine files from two unrelated locations into one batch action.
13. As a user, when I mark an entry in a different directory than my current marks belong to, I want the old directory's marks discarded and a fresh set started in the new directory, so that a batch action always targets exactly one directory's files.
14. As a user, I want sort, group, filter, and glob keys to simply do nothing while I have marks, so that reordering or hiding entries can never silently detach my marks from the files I actually meant to select.
15. As a user, I want `v` to be unavailable (with a clear message) while I'm browsing glob results, so that selection mode never has to make sense of marks spanning multiple real directories at once.
16. As a user, I want `v` to work normally while a filter is active, since a filter only narrows my current directory's own listing rather than mixing in files from elsewhere.

## Implementation Decisions

- `AppMode` gains `MODE_SELECT`. `v` toggles into/out of it from `MODE_NAV`; entering does not mark the current entry. Leaving via `v` or `Esc` discards all marks.
- `v` is rejected while `MODE_GLOB` is active: it produces the existing `MODE_ERROR`/`STYLE_ERROR` flow (`001-foundation`'s "any key dismisses" convention) with a message explaining selection mode isn't available on glob results, because glob results can span multiple real directories (`013-glob-inline-recursive-walk`). `v` is unaffected by an active filter (`MODE_FILTER`).
- `space` toggles the mark on the entry under the cursor (meaningful only inside `MODE_SELECT`).
- `r` implements anchor-then-extend range selection: the first press records the cursor's current entry as the anchor and captures that entry's marked/unmarked state at that moment. The operation's fixed target state is the inverse of the anchor's captured state, applied to every entry the cursor visits via arrow movement. This is sticky, not live: once an entry is set during the sweep, it keeps that state even if the cursor later moves back over it. `space` remains available throughout as an independent, immediate per-entry toggle. A second `r` press ends the extend and returns to plain `MODE_SELECT` navigation (anchor/target state discarded, marks already applied stay).
- `t` toggles select-all/select-none for the current directory's listing: if not everything is marked, it marks everything; if everything is already marked, it clears all marks.
- The marked set is stored alongside the directory it belongs to (not just a bare set of indices) — `Model` needs both the mark data and a "which directory" field.
- Navigating between directories while in `MODE_SELECT` does not force an exit and does not itself clear marks — marks can remain intact even while the directory they belong to is off-screen.
- The moment `space`/`r`/`t` marks an entry in a directory different from the one the current marks belong to, the old marks are discarded first and the new directory starts a fresh set.
- Sort, group, filter, and glob keys become no-ops whenever marks currently exist in `MODE_SELECT`. This sidesteps deciding whether marks should follow the file (by name/path) or the row (by index) across a resort/filter change.

## Testing Decisions

- Good tests here assert on `update()`'s return values given specific inputs, matching the existing convention (`001-foundation`) — never on terminal output or real filesystem state.
- Table-driven cases for: entering/leaving `MODE_SELECT` (including the glob-blocked case, and that `v` still works with an active filter), `space` toggling, the `r`-extend sequence (anchor capture, sweep, sticky-not-live backtracking, second-press stop), `t`'s tri-state behavior (mark-all when not all marked, clear when all marked), the directory-scoping/discard-on-cross-directory-mark rule (marking in a new directory discards the old set), and sort/group/filter/glob becoming no-ops with active marks.

## Out of Scope

- Any rendering of marked entries or the title/status line (chunk 3/4 and 4/4).
- Batch trash/delete/yank-copy/yank-move/paste wiring (chunk 2/4).
- Help text updates (chunk 4/4).
- The async action queue mechanism itself (`10`/`11-async-action-queue`).
- The specific single-entry operations' semantics (`003-copy-move`, `008-trash`).
- Exact new `Msg`/`Model`-field identifier names — left to implementation, only the shape and behavior are specified here.

## Further Notes

Chunk 1 of 4 in the multi-select feature (originally `docs/prd/triage/12-multi-select.md`), split so `prd-autopilot` can implement/verify/commit each layer independently instead of the whole feature in one pass — mirroring the precedent set by the git-status-colors PRD split (017-020). Depends on `001-foundation` (for `Model`/`Msg`/`Cmd` conventions) and `010`/`011-glob-mode` (for the glob-blocking rule). Chunks 2-4 (batch actions, visuals, status/title+help) all depend on this chunk's `MODE_SELECT`/marked-set state existing first. This PRD's UX was fully resolved via a dedicated `/grill-me` session — see the original combined PRD's Further Notes for that history.

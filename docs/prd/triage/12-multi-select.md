---
title: "Multi-select"
description: "Every action in dired today operates on exactly one entry — batch operations (delete/copy/move several files at once) aren't possible."
status: needs-triage
---

## Problem Statement

`selected` in the current `Model` is a single index — there's no way to mark several entries and act on all of them at once. Deleting, trashing, copying, or moving a batch of files requires repeating the same single-entry action once per file.

## Solution

Add a transient selection mode (entered/left with `v`) in which the user can mark multiple entries in the current directory, then apply an existing action key (trash, permanent delete, yank-copy, yank-move) to the whole marked set at once, instead of just the entry under the cursor.

## User Stories

1. As a user, I want to enter a selection mode with `v`, so that I can start marking multiple entries without changing what my normal navigation keys do.
2. As a user, I want `v` to only switch modes (not mark the entry under my cursor), so that entering selection mode never surprises me with an already-marked entry.
3. As a user, I want to leave selection mode with `v` or `Esc`, so that I have two familiar ways to bail out.
4. As a user, I want leaving selection mode without running a batch action to discard my marks, so that stale marks never leak into normal navigation.
5. As a user, I want `space` to toggle the mark on the entry under my cursor while in selection mode, so that I can mark files one at a time as I navigate.
6. As a user, I want a range-select gesture (`r`), so that I don't have to press `space` on every single row to mark a long contiguous run of files.
7. As a user, I want the range gesture's effect (mark vs. unmark) to be decided by the anchor entry's own state before I started the range, so that starting a range from an unmarked file extends marking and starting from an already-marked file extends unmarking (a "select more" or "deselect a run" operation, picked automatically).
8. As a user, I want `space` to still work independently while a range is active, so that I can hand-correct one entry without disrupting the sweep.
9. As a user, I want a second `r` press to stop the range extension (staying in selection mode), so that I can start a fresh gesture afterward.
10. As a user, I want a select-all/select-none key (`t`), so that I can quickly mark everything in the current directory or clear everything in one keystroke, without needing two separate keys.
11. As a user, I want my marks to survive navigating into a subdirectory or up to the parent while still in selection mode, so that browsing around doesn't silently wipe my work.
12. As a user, I want my marks to be understood as belonging to one specific directory, so that marking a file in a different directory doesn't try to combine files from two unrelated locations into one batch action.
13. As a user, when I mark an entry in a different directory than my current marks belong to, I want the old directory's marks discarded and a fresh set started in the new directory, so that a batch action always targets exactly one directory's files.
14. As a user, I want a batch action I trigger without marking anything new in the directory I'm currently looking at to still apply to my previous (now off-screen) directory's marks, so that browsing away to double check something doesn't cancel my pending selection.
15. As a user, I want trash (Backspace) and permanent delete (`x`) to act on all marked entries when I have marks, using the same keys I already use for single-entry delete, so that I don't have to learn new keybindings for the batch case.
16. As a user, I want yank-copy (`c`) and yank-move (`m`) to act on all marked entries when I have marks, and `p` to paste all of them, so that batch copy/move reuses the exact same flow as single-entry copy/move.
17. As a user, I want one combined confirmation prompt ("Delete N items?") for a batch delete/trash, instead of one prompt per file, so that clearing out many marked files doesn't require answering the same question repeatedly.
18. As a user, I want the status line to show what I've yanked as a count and operation ("Yanked: 3 items (copy)") when it's a batch, so that I have the same at-a-glance feedback I get for a single yank.
19. As a user, when multiple entries are marked, single-entry actions like rename never have to guess what to do with the rest, because those actions are only reachable outside selection mode, where no marks exist.
20. As a user, I want marked entries to render in a distinct color (blue) so I can see at a glance which files are part of my pending batch.
21. As a user, I want a marked entry that's also under my cursor to render clearly as both (reverse-video blue), so that cursor position and mark state are never ambiguous.
22. As a user, I want a mark to visually take priority over git-status coloring on the same row, so that a marked row is unambiguous even for a file that would otherwise show as modified/untracked/etc.
23. As a user, I want the title line to show I'm in selection mode and how many entries are marked ("VISUAL(3) : /path"), so that I always know my mode and progress without counting colored rows myself.
24. As a user, I want the title line to read plainly "VISUAL : /path" (no count) when nothing is marked yet, so the parenthesized count doesn't show a confusing "(0)".
25. As a user, I want sort, group, filter, and glob keys to simply do nothing while I have marks, so that reordering or hiding entries can never silently detach my marks from the files I actually meant to select.
26. As a user, I want `v` to be unavailable (with a clear message) while I'm browsing glob results, so that selection mode never has to make sense of marks spanning multiple real directories at once.
27. As a user, I want `v` to work normally while a filter is active, since a filter only narrows my current directory's own listing rather than mixing in files from elsewhere.
28. As a developer, I want the batch action's actual file-operation execution to defer entirely to the async action queue (`10`/`11-async-action-queue`), so that this PRD does not have to re-solve running N slow operations without blocking the UI.

## Implementation Decisions

**Mode and entry/exit:**
- `AppMode` gains `MODE_SELECT`. `v` toggles into/out of it from `MODE_NAV`; entering does not mark the current entry. Leaving via `v` or `Esc` discards all marks; leaving because a batch action ran also discards marks, as part of the same transition back to `MODE_NAV`.
- `v` is rejected while `MODE_GLOB` is active: it produces the existing `MODE_ERROR`/`STYLE_ERROR` flow (`001-foundation`'s "any key dismisses" convention) with a message explaining selection mode isn't available on glob results, because glob results can span multiple real directories (`013-glob-inline-recursive-walk`), which would break the one-marks-set-belongs-to-one-directory rule below. `v` is unaffected by an active filter (`MODE_FILTER`), since filtering only narrows the current directory's own listing.

**Marking primitives (meaningful only inside `MODE_SELECT`):**
- `space` toggles the mark on the entry under the cursor.
- `r` implements anchor-then-extend range selection: the first press records the cursor's current entry as the anchor and captures that entry's marked/unmarked state at that moment. The whole operation then has one fixed target state — the inverse of the anchor's captured state (anchor was unmarked → sweeping marks; anchor was marked → sweeping unmarks) — applied to every entry the cursor visits via arrow movement. This is sticky, not live: once an entry has been set during the sweep, it keeps that state even if the cursor later moves back over it (no recompute-on-backtrack). `space` remains available throughout as an independent, immediate per-entry toggle for manual correction. A second `r` press ends the extend and returns to plain `MODE_SELECT` navigation (anchor/target state discarded, marks already applied stay).
- `t` toggles select-all/select-none for the current directory's listing: if not everything is marked, it marks everything; if everything is already marked, it clears all marks.

**Scope and persistence:**
- The marked set is stored alongside the directory it belongs to (not just a bare set of indices) — `Model` needs both the mark data and a "which directory" field.
- Navigating between directories while in `MODE_SELECT` does not force an exit and does not itself clear marks — marks can remain intact even while the directory they belong to is off-screen.
- The moment `space` marks an entry in a directory different from the one the current marks belong to, the old marks are discarded first and the new directory starts a fresh set. A given batch action can only ever span the entries of one directory.
- A batch action triggered without first marking anything new in the currently-viewed directory (i.e., marks still belong to a previously-visited, now off-screen directory) targets that off-screen directory's marks.
- Sort, group, filter, and glob keys become no-ops whenever marks currently exist in `MODE_SELECT`. This sidesteps deciding whether marks should follow the file (by name/path, robust to reordering) or the row (by index, fragile) across a resort/filter change — the question doesn't need answering because those operations simply can't run while marks exist.

**Batch actions:**
- Trash (Backspace), permanent delete (`x`), yank-copy (`c`), and yank-move (`m`) reuse their existing keys and `Msg` handlers; scope (single entry vs. every marked entry) is decided by `mode == MODE_SELECT` plus a non-empty marked set, branching inside the existing handlers rather than introducing separate batch-specific `Msg` variants.
- Because marks only exist transiently inside `MODE_SELECT`, single-entry actions reachable only from `MODE_NAV` (rename, single delete/trash) never have to special-case "what if several are marked" — that state is unreachable from where they run.
- A batch trash/delete shows exactly one combined confirmation ("Delete N items? [y/N]") instead of one prompt per file.
- Yank state generalizes from a single pending path to an array of paths with a count, so paste (and the status-line yank display) has one code path that covers both the single-file case (count 1) and the batch case (count N) — the status line reads "Yanked: N items (copy|move)" for a batch.
- Actually dispatching a batch of N filesystem operations without blocking the UI is explicitly the async action queue's concern (`10`/`11-async-action-queue`), not solved here — see Out of Scope.

**Visuals:**
- Two new style tags are needed: marked (blue foreground, default background — same shape as an unselected git-status row, just blue instead of a git color) and marked+cursor (reverse video with blue as the background, analogous to how the plain cursor-row style already works).
- A mark takes full precedence over git-status coloring on the same row: unlike the existing cursor+git-status combination (which blends, using the git color as the reverse-video background per `020-git-status-colors-04-color-mapping`), a mark simply overrides the git color entirely — the row's git status doesn't show while it's marked.
- The style-selection logic (today a git-status-only switch) needs the marked check added with higher priority, ahead of the git-status branches.

**Status/title line:**
- The existing "Path: <current_path>" title line becomes "VISUAL(N) : <current_path>" whenever one or more entries are marked in `MODE_SELECT`, or plain "VISUAL : <current_path>" (no count) when in `MODE_SELECT` with zero marks. Outside `MODE_SELECT` it stays "Path: <current_path>" unchanged.

**Help text:** the in-app help string and `-help` CLI output both need entries for `v`/`space`/`r`/`t` and a short explanation of selection mode.

## Testing Decisions

- Good tests here assert on `update()`'s and `view()`'s return values given specific inputs, matching the existing convention (`001-foundation`) — never on terminal output or real filesystem state.
- `update()`: table-driven cases for entering/leaving `MODE_SELECT` (including the glob-blocked case), `space` toggling, the `r`-extend sequence (anchor capture, sweep, sticky-not-live backtracking, second-press stop), `t`'s tri-state behavior, the directory-scoping/discard-on-cross-directory-mark rule, sort/group/filter/glob becoming no-ops with active marks, and the batch-vs-single scope branch on trash/delete/yank handlers.
- `view()`: table-driven cases for the two new style tags (marked, marked+cursor) including their precedence over git-status tags, and for the title-line's three states (`Path: ...` / `VISUAL : ...` / `VISUAL(N) : ...`).
- The actual batch filesystem `Cmd` execution (fork/exec mechanics) is out of scope for this PRD's tests, same as it's out of scope for the design — it's the async action queue's testing surface once that PRD is built.

## Out of Scope

- The async action queue mechanism itself (`10`/`11-async-action-queue`) — multi-select is deliberately sequenced last in the roadmap, after the queue exists. Batch trash/delete/copy/move for N>1 marked entries are meant to run through it once it lands, not synchronously in a blocking loop; this PRD does not solve how N queued operations actually execute without blocking the UI.
- The specific single-entry operations' semantics (`003-copy-move`, `008-trash`) — this PRD only adds a batch scope on top of them.
- Exact new `Msg`/`StyleTag`/`Model`-field identifier names — left to implementation, only the shape and behavior are specified here.
- A configurable mark color or user-facing color legend.

## Further Notes

Depends on `001-foundation` (for `Model`/`Msg`/`Cmd`/style-tag conventions) and `010`/`011-glob-mode` (for the glob-blocking rule) and `020-git-status-colors-04-color-mapping` (for the precedence rule against git-status coloring). Conceptually paired with `10`/`11-async-action-queue`, which is this PRD's main dependency for actually executing a batch — sequenced after it deliberately, once there's a real need for it.

This PRD's UX was fully resolved via a dedicated grilling session (previously it was in draft form from a high-level roadmap discussion only). The four originally-open questions — marking/range/select-all keybindings, visual distinction from cursor-row highlighting, whether single-entry actions still apply when things are marked, and whether marks persist across navigation — are all answered above.

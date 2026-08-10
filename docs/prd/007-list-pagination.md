---
title: "Paginate the entry list when it's taller than the terminal"
description: "view() draws every entry unconditionally and render() paints them at sequential rows with no bound, so a directory with more entries than terminal rows overflows off-screen with no way to scroll to the rest."
status: done
---

## Problem Statement

`view()` builds one `Line` per entry in `model->entry_count` with no upper
bound, and `render()` paints each at row `i` starting from 0 with no
awareness of terminal height — only the final line (the help bar) is
pinned to `tb_height() - 1`. Neither function ever compares the number of
entries to the number of rows actually available. In a directory with
more entries than the terminal is tall, the excess rows are simply drawn
past the bottom of the screen (or collide with the pinned help bar) and
are never reachable — there's no way to scroll down and see them, and no
indication that they even exist.

## Solution

Add page-snap pagination to the entry list: once the cursor moves past
the last row currently visible, the view jumps to the next full page of
entries (and symmetrically backward), the same way a classic pager pages
through content in fixed-size chunks. The `Path:`/prompt lines at the top
and the help bar at the bottom always stay pinned exactly as they do
today — only the entry rows between them scroll. A `<-`/`->` marker at
the rightmost column of the first/last visible entry row tells the user
when there's more content above/below the current page. Terminal size
becomes part of `Model`, kept in sync via a resize message, so the
pure `update()`/`view()` functions never call into termbox2 directly.

## User Stories

1. As a user, I want to see every entry in a directory that has more entries than my terminal has rows, so that a large directory is never partially unreachable.
2. As a user, I want the cursor to page forward once it reaches the bottom of the current page, so that moving down through a long list naturally reveals the next chunk.
3. As a user, I want the cursor to page backward once it reaches the top of the current page, so that moving up through a long list naturally reveals the previous chunk.
4. As a user, I want the `Path:` line, the status/prompt line, and the help bar to stay exactly where they are today regardless of pagination, so that only the file listing itself scrolls.
5. As a user, I want a `<-` marker on the right edge of the first visible entry row when there are more entries above the current page, so that I know scrolling further up will reveal more.
6. As a user, I want a `->` marker on the right edge of the last visible entry row when there are more entries below the current page, so that I know scrolling further down will reveal more.
7. As a user, I want no marker at all when the current page is the only page (every entry already fits), so that short directories look exactly as they do today.
8. As a user, when I press `n` to create a new file while scrolled into the middle of a long list, I want the page I'm currently looking at to stay exactly where it is, so that starting to type a new filename doesn't yank my view away to wherever the end of the list happens to be.
9. As a user, I want the in-progress new-file row to always be visible in its own reserved line right below the current page while I'm typing, so that I can always see what I'm creating regardless of where the page is scrolled.
10. As a user, I want the `<-`/`->` markers to never appear on that in-progress new-file row, so that markers are only ever a statement about the real entries, never about the row I'm currently typing into.
11. As a user, if I resize my terminal, I want the next render to page against the new size, so that the list still fills the available rows correctly instead of using stale dimensions from before the resize.
12. As a user, I don't expect the app to behave gracefully in a terminal too short to fit the fixed chrome (path/prompt/help lines) — that's an accepted limitation, not a bug.

## Implementation Decisions

- **`Model` gains three new `int` fields:** `term_height`, `term_width`, `scroll_offset`. All are runtime/session state (not persisted), following the same convention as `sort_mode`/`group_mode`/`show_hidden`.
- **New pure helpers in `helpers.c`/`helpers.h`** (deep modules, alongside `is_protected_name`/`is_hidden_name`):
  - `visible_entry_rows(term_height, has_virtual_line)` — returns how many entry rows fit: `term_height` minus the 3 always-fixed chrome lines (`Path:`, prompt/status, help bar), minus one further row when `has_virtual_line` is true (the reserved create-mode row). This is the single source of truth for that arithmetic; both `update.c` and `view.c` call it rather than duplicating the subtraction.
  - `page_snap_offset(selected, entry_count, visible_rows)` — the page-snap formula (`floor(selected / visible_rows) * visible_rows`, clamped so the offset never runs past `entry_count`). Isolated from `Model`/`Msg` entirely so it's directly table-testable.
- **New `MsgType`: `MSG_RESIZE`.** Carries the new terminal width/height (mirrors how `MsgDirLoaded` carries its payload in the `Msg` union). Raised by `translate_event()` in `dired.c` from `TB_EVENT_RESIZE` (using the event's `w`/`h` fields), the only new case added there.
- **`update()` handles `MSG_RESIZE` at the top-level switch**, short-circuiting with a `return` exactly like the existing `MSG_DIR_LOADED`/`MSG_OP_SUCCEEDED`/`MSG_OP_FAILED` cases: sets `term_height`/`term_width` on `out_model`, then recomputes `scroll_offset`.
- **Startup seeding:** `main()` in `dired.c` calls `tb_height()`/`tb_width()` once right after `tb_init()`, before entering the loop, and stores them directly on the initial `model` — the same pattern already used to seed `model.current_path` via `getcwd()`. Without this, `term_height`/`term_width` would stay `0` until the user's first actual terminal resize.
- **`scroll_offset` recompute triggers:** `update()` recomputes `scroll_offset` (via `page_snap_offset`) after `MSG_MOVE_UP`, `MSG_MOVE_DOWN`, `MSG_RESIZE`, and any path that reloads/relocates the entry list (`MSG_DIR_LOADED`, the sort/group cycle relocation, the hidden-files toggle reload). It is explicitly **not** recomputed when entering create mode (`MSG_NEW`/`start_edit()`, which sets `selected = entry_count`) — the page stays exactly where it was, satisfying user story 8.
- **`view()` slices instead of iterating the full array:** the current unconditional `for (i = 0; i < entry_count; i++)` loop in `view.c` becomes a slice over `[scroll_offset, scroll_offset + visible_entry_rows(...))`, clamped to `entry_count`. The virtual line (create mode), when present, is appended after the sliced entries exactly as today, unaffected by `scroll_offset`.
- **Markers are embedded directly in `Line.text` by `view()`**, not drawn separately by `render()`: since `term_width` now lives on `Model`, `view()` right-pads the first visible entry's line (when `scroll_offset > 0`) and the last visible *real* entry's line (when more real entries exist past the current page) out to `term_width` columns and appends `<-`/`->` respectively. `render()` in `dired.c` is unmodified — it keeps blitting whatever `view()` returns, exactly as it does today.
- **Marker scope:** only ever applied to real entry rows, never to the virtual (create-mode) row, per user story 10.

## Testing Decisions

- Tests only assert on external behavior — function inputs/outputs and `Msg`/`Model` pairs — never internal call sequences, matching the existing style throughout `test/`.
- **`visible_entry_rows()` and `page_snap_offset()`:** table-driven unit tests in `helpers_test.c`, same style as `test_is_protected_name`/the hidden-files predicate tests — covering exact-fit, more-than-fits, fewer-than-fits, the `has_virtual_line` row deduction, and boundary offsets (selected at the very start/end of a page).
- **`MSG_RESIZE` handling and `scroll_offset` recompute:** table-driven `update()` tests in `update_test.c`, same harness as `test_move_selection`/`test_cycle_sort_wraps_through_all_eight_states` — asserting `term_height`/`term_width` land on the resulting model after `MSG_RESIZE`, that `scroll_offset` advances a full page on `MSG_MOVE_DOWN` crossing a page boundary (and retreats on `MSG_MOVE_UP` crossing back), and that entering create mode (`MSG_NEW`) leaves a non-zero `scroll_offset` untouched.
- **`view()` slicing and markers:** extend `view_test.c` in the same style as `test_view_nav_listing`/`test_view_create_virtual_row` — a model with more entries than `visible_entry_rows()` allows, asserting only the expected slice of names appears; a scrolled-into-the-middle case asserting both `<-` and `->` appear at the rightmost column; a first-page and last-page case each asserting only the applicable marker appears; and a create-mode case (mid-list, scrolled) asserting the virtual row appears in its own line with no marker while the page underneath is unchanged from before `MSG_NEW`.
- **Not tested:** the real termbox2 event loop in `dired.c` (`tb_init`/`tb_poll_event`/`TB_EVENT_RESIZE` wiring, the startup `tb_height()`/`tb_width()` seed, and `render()`'s blitting) — consistent with `dired.c` being the untested impure shell throughout the existing test suite.

## Out of Scope

- Graceful behavior in a terminal too short to fit the fixed chrome (path/prompt/help lines) — accepted as a non-functioning edge case, no clamping or special-casing added.
- Any pagination strategy other than page-snap (centered scrolling, minimal/one-row-at-a-time scrolling) — explicitly decided against in favor of page-snap's stateless-formula simplicity.
- A numeric position indicator (e.g. `12-40/340`) — only the `<-`/`->` edge markers are added.
- Horizontal scrolling or truncation of overly long filenames/paths — unrelated to this vertical-pagination problem.
- Persisting `scroll_offset`, `term_height`, or `term_width` across restarts — all three are runtime-only, reset on every launch same as the rest of the non-persisted `Model` state.

## Further Notes

Depends on `001-foundation` (the `Model`/`Msg`/`Cmd`/`update()`/`view()` split) and follows the same "impure shell seeds initial state, pure core reacts to messages" convention already used for `current_path`/`getcwd()`. The create-mode "don't recompute the page" behavior is a deliberate exception to the general recompute-on-`selected`-change rule, not an oversight — flagged explicitly here since `start_edit()` already mutates `selected` today and it would be easy to wire the recompute too broadly and regress user story 8.

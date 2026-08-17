---
title: "Stop pinning the last listing row to the screen bottom"
description: "render() force-places the last View line at tb_height()-1 instead of its sequential row, leaving a visible gap before it whenever the listing doesn't fill the terminal."
status: done
---

## Problem Statement

When the directory listing has fewer entries than fit on screen, the
last entry in the listing renders detached at the very bottom of the
terminal, with a large blank gap between it and the rest of the
listing, instead of appearing right after the previous entry.

## Solution

Every line built by `view()` should render at its natural sequential
row. The last line should not be special-cased to the terminal's last
row — when the listing exactly fills the screen, sequential placement
already lands the final entry on the terminal's last row on its own
(`visible_entry_rows()` already reserves exactly 2 header rows, so a
full listing's last entry naturally falls at `term_height - 1`).

## User Stories

1. As a user, I want every row of the directory listing to appear
   directly below the previous row, so that the listing reads as one
   continuous list instead of having a stray detached last line.
2. As a user browsing a short directory, I want the listing to end
   right after its last entry, not leave a large blank gap followed by
   one line pinned to the bottom of the screen.
3. As a user browsing a directory that exactly fills the screen, I
   want the behavior to be unchanged — the last entry should still
   land on the terminal's last row, just as a natural consequence of
   sequential placement, not a special case.

## Implementation Decisions

- Root cause (already diagnosed): `render()` in `src/dired.c` computes
  `row = (i == v.line_count - 1) ? tb_height() - 1 : i`, forcing
  whatever line happens to be last in the `View` (normally the last
  visible directory entry, or the virtual create/rename line when
  active) onto the terminal's absolute last row, regardless of how
  many lines precede it.
  This predates the current `view.c` line-list architecture (confirmed
  via `git log -L` on the surrounding lines) and appears to be a stale
  special case carried forward from an earlier rendering design, not
  something the current `view()`/`View` line-list shape depends on.
- Fix: render every line at its sequential index (`row = i`) with no
  special case for the last line. `visible_entry_rows()`
  (`term_height - 2 - has_virtual_line`) already accounts for the 2
  header rows, so a listing that fills the available space still ends
  exactly on the terminal's last row under plain sequential placement
  — the special case is provably redundant in the full-screen case and
  actively wrong in the short-listing case.
- No changes needed to `view.c`, scrolling/pagination
  (`page_snap_offset`), or `visible_entry_rows()` — this is confined to
  the row computation inside `render()`.

## Testing Decisions

- This is a terminal-rendering concern (`tb_print` row placement), not
  something `view()`'s pure `View` construction covers today — `view()`
  already returns lines in the correct order regardless of this bug.
  Prior art: `test/integration/` drives the real `diredd` binary under
  a pseudo-terminal (per PRD 030/032/033) and can assert on rendered
  screen content/row positions.
- Add an integration test that opens a directory with a small number
  of entries (fewer than fit in the test terminal's height) and
  asserts every entry appears on a contiguous run of rows starting
  right after the header rows, with no entry pinned to the terminal's
  last row while a gap of blank rows precedes it.
- Add/keep a case covering a listing that exactly fills the terminal
  height, asserting the last entry still lands on the terminal's last
  row (regression guard for the case the old special-case code was
  presumably trying to handle).

## Out of Scope

- Any change to scrolling, pagination, or `page_snap_offset()` behavior.
- Any change to `view_picker()`, which has no equivalent last-line
  special case and is not affected by this bug.

## Further Notes

Full investigation and file/line evidence: `docs/bugs/001-history-persistence-and-pinned-last-row.md`, "Bug B".

---
title: "Git status: view-layer style tags (chunk 3/4)"
description: "Entry.git_status is now populated from real git data, but the view layer has no style tags for it and never looks at the field."
status: done
---

## Problem Statement

`Entry.git_status` is now populated with real data (chunk 2), but
`view()` has no notion of git-aware styling — it only ever emits
`STYLE_NORMAL`/`STYLE_SELECTED`. There is nowhere yet for a git state to
be represented as a `StyleTag`.

## Solution

Extend `StyleTag` (the same semantic style-tag mechanism the codebase
already uses for selection) with 8 new values — one pair (plain and
"_selected") for each of Conflicted/Modified/Untracked/Ignored — and have
`add_entry_line()`/`add_virtual_line()` pick among these based on an
entry's `git_status` and whether it's the selected row. This chunk stops
at tag selection; `style_colors()` in `dired.c` still needs to map these
new tags to real colors (chunk 4) before anything visibly changes.

## User Stories

1. As a user, I want the selected row to still be visually distinguishable
   as "selected" even when it's also styled by git status, so that I
   never lose track of my cursor position.
2. As a user, I want a selected git-styled row to also still carry which
   git state it's in, so that selecting an entry doesn't hide the very
   information the styling exists to show.
3. As a user, I want a clean file or folder to keep using today's
   `STYLE_NORMAL`/`STYLE_SELECTED` tags, so that the common case stays
   visually quiet and only exceptions stand out.
4. As a developer, I want `view()` to remain the single place deciding
   which semantic tag an entry line gets, so that this feature slots into
   the existing architecture rather than introducing a second styling
   mechanism.

## Implementation Decisions

- **`view.h`/`view.c` (modified):** `StyleTag` gains 8 new values — one
  pair (plain and "_selected") for each of
  Conflicted/Modified/Untracked/Ignored. `add_entry_line()` and
  `add_virtual_line()` pick among these (or fall back to today's
  `STYLE_NORMAL`/`STYLE_SELECTED` for `GIT_STATUS_NONE`) based on the
  entry's `git_status` and whether it's the selected row — the same place
  that already decides `STYLE_NORMAL` vs `STYLE_SELECTED` today.
- No changes to `dired.c`/`style_colors()` in this chunk — the new tags
  exist and are emitted, but until chunk 4 maps them to fg/bg colors they
  will fall through however unmapped tags are currently handled in
  `style_colors()` (verify current fallback behavior rather than assuming
  it's a crash — likely an unstyled/default render, which is acceptable
  as an intermediate state for this chunk).

## Testing Decisions

- `view()`'s tag-selection logic (choosing the right `StyleTag` from an
  entry's `git_status` and selection state) extends the existing
  table-driven pattern in `test/view_test.c` (e.g. `test_nav_listing`),
  the same way it already asserts `STYLE_SELECTED` vs `STYLE_NORMAL`
  today — cover each of the 4 non-clean states, both selected and
  unselected, plus confirmation that `GIT_STATUS_NONE` still yields
  today's `STYLE_NORMAL`/`STYLE_SELECTED`.

## Out of Scope

- Mapping the new `StyleTag` values to actual fg/bg colors (chunk 4).
- A color legend in the help bar or anywhere else in the UI.
- Any indication that git integration is inactive, beyond falling back to
  today's tags for `GIT_STATUS_NONE`.

## Further Notes

This is chunk 3 of 4 for the "Color entries by git status" feature.
Depends on chunk 1 (`GitStatusTag`/`Entry.git_status`) and chunk 2
(real data being populated) to be meaningfully testable end-to-end, though
the `view()` unit tests can exercise the new tag-selection logic directly
against a hand-built `Entry` regardless of whether `loaddir` wiring has
landed. Chunk 4 makes the result visible by mapping these tags to colors
in `style_colors()`.

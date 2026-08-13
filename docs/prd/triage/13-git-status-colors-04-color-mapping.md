---
title: "Git status: color mapping (chunk 4/4)"
description: "view() now emits git-aware StyleTags, but style_colors() doesn't map them to real termbox2 colors yet, so nothing visibly changes on screen."
status: needs-triage
---

## Problem Statement

`view()` now emits 8 new `StyleTag` values for git states (chunk 3), and
`Entry.git_status` is populated from real `git status` output (chunk 2),
but `style_colors()` in `dired.c` — the single place that maps a
`StyleTag` to actual fg/bg termbox2 colors — has no entries for them.
Nothing is visibly colored yet.

## Solution

Add fg/bg color mappings in `style_colors()` for the 8 new tags:
Conflicted = red, Modified = green, Untracked = yellow, Ignored =
dim/dark gray. This is the final chunk — once it lands, the full feature
described in the parent PRD is visibly working end-to-end.

## User Stories

1. As a user, I want a file with unstaged or staged changes to render in
   a distinct color, so that I can spot what I've edited without leaving
   the listing.
2. As a user, I want an untracked file to render in its own distinct
   color, so that I can spot new files I haven't added to git yet.
3. As a user, I want an ignored file to render in its own distinct, muted
   color, so that I can tell at a glance it's intentionally excluded from
   git rather than just clean.
4. As a user, I want a file with a merge conflict to render in its own
   distinct, attention-grabbing color, so that I can find conflicts
   quickly.
5. As a user, I want a folder containing changes anywhere below it to
   render with a color, so that I don't have to open every subdirectory
   to find out something changed.
6. As a user, I want a clean file or folder to render exactly as it does
   today, so that the common case stays visually quiet.
7. As a user, I want a folder or file outside of any git repository to
   render exactly as it does today, so that non-repo browsing isn't
   cluttered with a "not applicable" indicator.
8. As a user, I want a selected git-colored row to still show which git
   state it's in, with "selected" still clearly readable, so that
   selecting an entry doesn't hide the very information the coloring
   exists to show.
9. As a developer, I want `style_colors()` in `dired.c` to remain the
   single place mapping a tag to actual fg/bg colors, so that this
   feature slots into the existing architecture rather than introducing a
   second styling mechanism.

## Implementation Decisions

- **`dired.c` (modified):** `style_colors()` gains the fg/bg mapping for
  the 8 new tags. Unselected: git color as foreground, default
  background. Selected: the "_selected" variants swap this — the git
  color becomes the background, with black or white foreground
  (whichever contrasts) — the same reverse-video idea `STYLE_SELECTED`
  already uses with white, just parameterized by the git color instead.
- **Colors:** Conflicted = red, Modified = green, Untracked = yellow,
  Ignored = dim/dark gray, Clean = default (no color change, falls
  through to existing `STYLE_NORMAL`/`STYLE_SELECTED` mapping already in
  `style_colors()`).
- **Help text:** unchanged — no color legend is added to the help bar or
  elsewhere.

## Testing Decisions

- `style_colors()` in `dired.c` is not unit tested, matching the existing
  untested status of its `STYLE_SELECTED`/`STYLE_PROMPT`/`STYLE_ERROR`
  mappings today.
- Manual verification: in a real repo directory with a mix of
  modified/untracked/ignored/conflicted/clean files, confirm each state
  renders in its documented color, that selecting a git-colored row keeps
  it legible and still shows its git color, and that a non-repo directory
  renders exactly as before this feature existed.

## Out of Scope

- A color legend in the help bar or anywhere else in the UI.
- Any indication that git integration is inactive, beyond simply
  rendering uncolored, same as today.
- User-configurable color palettes (e.g. respecting `LS_COLORS` or a
  config file) — this codebase has no persistence/config layer and none
  is introduced here.

## Further Notes

This is chunk 4 of 4 — the final chunk — for the "Color entries by git
status" feature. Depends on chunk 3 (the new `StyleTag` values existing
and being emitted by `view()`). Once this lands, all sixteen user stories
from the original `13-git-status-colors` PRD are satisfied.

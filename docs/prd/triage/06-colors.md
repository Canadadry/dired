---
title: "Colors by file type"
description: "Every entry renders identically regardless of whether it's a directory, executable, symlink, or regular file, making the listing harder to scan visually."
status: needs-triage
---

> **Draft status:** written from the high-level architecture/roadmap grilling session only — this feature was only named ("couleurs") as a pick among several suggestions, never discussed in detail. Needs a dedicated grilling session before it's implementation-ready.

## Problem Statement

The entry listing shows permissions and size as text, but nothing visually distinguishes a directory from a regular file, an executable, or a symlink at a glance.

## Solution

Extend the semantic style-tag mechanism already established in `01-foundation` (`view()` emits tags like `STYLE_SELECTED`; `main()` maps tags to real termbox2 attributes) with additional tags representing file categories, so directories/executables/symlinks/regular files render with distinct colors.

## User Stories

1. As a user, I want directories to look visually distinct from regular files, so that I can scan a listing quickly.
2. As a user, I want executables to be visually distinct, so that I can spot runnable files.
3. As a user, I want symlinks to be visually distinct, so that I know when an entry points elsewhere.

## Implementation Decisions

**Decided:**
- This reuses the semantic style-tag mechanism from the foundation PRD rather than introducing a new styling concept — `view()` continues to emit tags, `main()` continues to be the only place mapping tags to termbox2 colors. This makes the feature low-cost since the plumbing already exists.

**Not yet decided — needs dedicated grilling:**
- The exact category set (directory/executable/symlink/regular — anything else? broken symlinks? special files?) and which color each maps to.
- Whether the color palette is configurable by the user (e.g. respecting `LS_COLORS`) or hardcoded.
- How file-category color composes with the existing `STYLE_SELECTED` tag (reverse video) — does selection override the category color, or combine with it?

## Testing Decisions

- Not yet decided. If a pure function decides "which category tag applies to this entry" (e.g. from `Entry.st.st_mode`), that's a natural table-driven test target, similar to the existing `mode_to_str`.

## Out of Scope

- User-configurable palettes, unless the dedicated grilling session decides otherwise.

## Further Notes

Depends on `01-foundation`. Judged small/cheap by the user specifically because the semantic-tag plumbing already exists — the main open work is deciding the category-to-color mapping.

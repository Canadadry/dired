---
title: "Multi-select: marked-entry rendering (chunk 3/4)"
description: "Marks exist (chunk 1/4) but render invisibly. Add the two new style tags so a marked row is visually distinct and takes precedence over git-status coloring."
status: done
---

## Problem Statement

With chunk 1/4 landed, entries can be marked, but nothing in `view()` reflects that — a marked row looks identical to an unmarked one. Users need a clear, unambiguous visual signal for which entries are part of their pending batch, including when the cursor sits on a marked row and when a marked row would otherwise show git-status coloring.

## Solution

Add two new style tags — marked and marked+cursor — and give the marked check higher priority than the existing git-status branches in the style-selection logic, so a mark always wins visually over git-status coloring on the same row.

## User Stories

1. As a user, I want marked entries to render in a distinct color (blue) so I can see at a glance which files are part of my pending batch.
2. As a user, I want a marked entry that's also under my cursor to render clearly as both (reverse-video blue), so that cursor position and mark state are never ambiguous.
3. As a user, I want a mark to visually take priority over git-status coloring on the same row, so that a marked row is unambiguous even for a file that would otherwise show as modified/untracked/etc.

## Implementation Decisions

- Two new style tags are needed: marked (blue foreground, default background — same shape as an unselected git-status row, just blue instead of a git color) and marked+cursor (reverse video with blue as the background, analogous to how the plain cursor-row style already works).
- A mark takes full precedence over git-status coloring on the same row: unlike the existing cursor+git-status combination (which blends, using the git color as the reverse-video background per `020-git-status-colors-04-color-mapping`), a mark simply overrides the git color entirely — the row's git status doesn't show while it's marked.
- The style-selection logic (today a git-status-only switch) needs the marked check added with higher priority, ahead of the git-status branches.

## Testing Decisions

- Good tests here assert on `view()`'s return values given specific inputs, matching the existing convention (`001-foundation`) — never on terminal output.
- Table-driven cases for the two new style tags (marked, marked+cursor) including their precedence over every git-status tag (conflicted/modified/untracked/ignored/clean), and for an unmarked row's styling being completely unaffected by chunk 1/4's `MODE_SELECT` existing.

## Out of Scope

- Batch action wiring (chunk 2/4).
- The title/status line mode indicator and help text (chunk 4/4).
- A configurable mark color or user-facing color legend.
- Any change to the git-status color legend, tag priority ordering, or `style_colors()` mappings established in PRD 021 for rows that aren't marked.

## Further Notes

Chunk 3 of 4 in the multi-select feature (originally `docs/prd/triage/12-multi-select.md`). Depends on chunk 1/4 (the marked set existing in `Model`) and `020-git-status-colors-04-color-mapping` (for the precedence rule against git-status coloring). Independent of chunk 2/4 (batch actions) — either can be implemented first once chunk 1/4 lands.

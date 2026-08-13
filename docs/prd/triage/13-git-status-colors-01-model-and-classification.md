---
title: "Git status: data model and pure classification (chunk 1/4)"
description: "No representation exists yet for an entry's git status, and there is no pure logic to derive one from `git status` output — add both, with zero filesystem/process access."
status: needs-triage
---

## Problem Statement

Nothing in the codebase can represent or compute an entry's git status yet.
Before any wiring or rendering work can happen, there needs to be (1) a
place on `Entry` to hold that status, and (2) a pure function that turns
raw `git status --porcelain --ignored -uall` text plus a directory's entry
names into a per-entry classification — independent of any subprocess or
filesystem call, so the classification rules can be exhaustively unit
tested.

## Solution

Add a `GitStatusTag` enum and an `Entry.git_status` field to `model.h`.
Add a new pure module `gitstatus` that takes porcelain-format status text
plus the current directory's entry names and returns a `GitStatusTag` per
entry.

## User Stories

1. As a developer, I want the git-status classification (parsing `git
   status` output and aggregating it into a per-entry tag) to be a pure
   function with no filesystem or process access, so that every
   priority-ordering and aggregation rule can be verified without a real
   repo or subprocess.
2. As a user, I want a folder containing changes anywhere below it — not
   just directly inside it — to be classified as changed, so that I don't
   have to open every subdirectory to find out something changed
   (classification only in this chunk; rendering lands in chunk 3/4).
3. As a user, when a folder contains a mix of different git states among
   its descendants, I want it classified by the single most important one
   (conflicted, then modified, then untracked, then ignored, then clean),
   so that the classification always points at the thing most worth
   attention.
4. As a developer, I want a symlink to be matched by its own path only —
   never treated as a prefix to descend through — so that classifying it
   never requires following it into a possibly unrelated part of the
   filesystem.

## Implementation Decisions

- **`model.h` (modified):** `Entry` gains a `GitStatusTag git_status`
  field; the `GitStatusTag` enum is defined here alongside `Entry`:
  `GIT_STATUS_NONE`, `GIT_STATUS_IGNORED`, `GIT_STATUS_UNTRACKED`,
  `GIT_STATUS_MODIFIED`, `GIT_STATUS_CONFLICTED`, ordered so a numeric max
  also gives the priority winner.
- **New module `gitstatus` (pure, no I/O):** takes the raw text of
  `git status --porcelain --ignored -uall` plus the current directory's
  entry names, and returns a `GitStatusTag` for each entry. Files match a
  porcelain line by exact relative path; directories match by prefix
  against every porcelain line and take the highest-priority match found.
  Symlinks are matched like any other entry (by their own path) and are
  never treated as a prefix to descend through.
- **States tracked, in priority order (highest wins when a directory's
  descendants mix states):** Conflicted → Modified (staged and unstaged
  merged into one state) → Untracked → Ignored → Clean.
- Nothing calls this module yet — no subprocess is spawned, `loaddir` is
  untouched, and `Entry.git_status` is set nowhere outside tests. Wiring
  it up to real `git status` output is the next chunk.

## Testing Decisions

- `gitstatus`'s classification/aggregation logic gets heavy table-driven
  unit tests (new `test/gitstatus_test.c`, same `Case[]` + loop style as
  `test/helpers_test.c`): canned porcelain-output blobs paired with entry
  name lists, covering each of the 5 states individually, priority
  ordering when a directory's descendants mix states, recursive
  aggregation through nested subdirectories, a file that exact-matches a
  porcelain path, and a directory-vs-symlink-with-the-same-name-prefix
  case confirming symlinks aren't treated as a prefix to descend through.

## Out of Scope

- Calling `git status` as a subprocess, or wiring `loaddir` to populate
  `Entry.git_status` (chunk 2).
- Any rendering/coloring of entries (chunks 3 and 4).
- Following symlinked directories to aggregate status through them.
- Splitting "Modified" into separate staged vs. unstaged states.

## Further Notes

This is chunk 1 of 4 for the "Color entries by git status" feature,
split so each layer of the implementation (data model, wiring, style
tags, color mapping) lands as its own reviewable, testable step. Depends
on `01-foundation` (the `Model`/`Entry` architecture). Chunk 2 wires this
module to real `git status` output via `loaddir`; chunk 3 extends
`StyleTag`/`view()` to pick a tag from `git_status`; chunk 4 maps those
tags to actual colors in `style_colors()`.

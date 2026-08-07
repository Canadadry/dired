---
title: "File preview panel"
description: "Navigating dired gives no indication of a file's content until you open it in vim, so choosing the right file among several similarly-named ones means opening each one."
status: needs-triage
---

> **Draft status:** written from the high-level architecture/roadmap grilling session only. This PRD has NOT had its own dedicated grilling pass — most of the "how" below is undecided. Needs a focused grilling session before it's implementation-ready.

## Problem Statement

Today, the only way to see what's inside a file is to open it in vim, which
takes over the whole screen and requires quitting back out to keep browsing.
There's no quick way to glance at a file's content while navigating.

## Solution

Add a preview panel that shows the content of the currently-selected file
without leaving the navigation screen. This is prioritized ahead of
copy/move in the roadmap because the user judged it higher-value.

## User Stories

1. As a user, I want to see a preview of the selected file's content while browsing, so that I can identify the right file without opening each one.
2. As a user, I want the preview to update as I move the selection up/down, so that I don't have to trigger it manually for every file.
3. As a developer, I want `view()`'s output reworked to describe multiple panels/zones (entry list + preview), so that `main()` can paint a split layout instead of a single list of lines.

## Implementation Decisions

**Decided:**
- The rework of `view()`'s output type — from a single flat list of styled lines (as shipped in the foundation PRD) to multiple panels/zones — happens inside this PRD, not anticipated earlier in the foundation.

**Not yet decided — needs dedicated grilling:**
- Layout: split direction (side-by-side vs top/bottom), fixed vs proportional panel sizes, whether the preview panel can be toggled off.
- What counts as "previewable": text files only, or also attempt binary files? How is that detected?
- Size/line limits on what gets read and shown (a large file shouldn't be read in full).
- Whether previewing a directory shows its contents (a mini listing) instead of file content.
- Whether preview reading is synchronous (blocks like the other v1 `Cmd`s) or needs to consider the same "could this be slow" question already answered "no" for other current operations.
- Whether the preview scrolls independently of the entry list, and what key(s) control that.
- Error/empty states in the preview panel (permission denied, empty file, binary file rejected).

## Testing Decisions

- Not yet decided. Whatever new pure logic is introduced (e.g. deciding what to display in the preview panel given a `Model`) should follow the same table-driven pattern established in the foundation PRD; the actual file-read `Cmd` execution stays untested (I/O shell), same as `load_directory` today.

## Out of Scope

- Any editing capability inside the preview panel — it's read-only.
- Copy/move (deferred to its own PRD, sequenced after this one).

## Further Notes

Depends on `01-foundation` (Model/Msg/Cmd, `update`/`view` split) shipping first. This is the PRD most likely to change shape once grilled in detail, since almost nothing about its UX was actually discussed yet — only that it exists and that it's the vehicle for reworking `view()`'s output shape.

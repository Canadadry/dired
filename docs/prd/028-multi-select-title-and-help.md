---
title: "Multi-select: title line and help text (chunk 4/4)"
description: "Selection mode (chunk 1/4) and its keys have no on-screen indicator of mode/progress, and no documentation. Add the VISUAL title-line states and help text for v/space/r/t."
status: done
---

## Problem Statement

Once chunks 1-3 land, selection mode is fully functional but invisible at the app-chrome level: the title line still reads "Path: <path>" while in `MODE_SELECT`, giving no indication the user is even in selection mode or how many entries are marked. Neither the in-app help string nor the `-help` CLI output mentions `v`/`space`/`r`/`t` at all.

## Solution

Change the title line to reflect `MODE_SELECT` and its mark count, and document the four new keys in both the in-app help and `-help` CLI output.

## User Stories

1. As a user, I want the title line to show I'm in selection mode and how many entries are marked ("VISUAL(3) : /path"), so that I always know my mode and progress without counting colored rows myself.
2. As a user, I want the title line to read plainly "VISUAL : /path" (no count) when nothing is marked yet, so the parenthesized count doesn't show a confusing "(0)".

## Implementation Decisions

- The existing "Path: <current_path>" title line becomes "VISUAL(N) : <current_path>" whenever one or more entries are marked in `MODE_SELECT`, or plain "VISUAL : <current_path>" (no count) when in `MODE_SELECT` with zero marks. Outside `MODE_SELECT` it stays "Path: <current_path>" unchanged.
- The in-app help string and `-help` CLI output both need entries for `v`/`space`/`r`/`t` and a short explanation of selection mode, following the existing help text's format and level of detail for other keys (see `Readme.md`'s Controls section and wherever the in-app help/`-help` text is generated in the codebase).

## Testing Decisions

- Good tests here assert on `view()`'s return values given specific inputs, matching the existing convention (`001-foundation`) — never on terminal output.
- Table-driven cases for the title line's three states (`Path: ...` / `VISUAL : ...` / `VISUAL(N) : ...`).
- Help text content itself (a static string) isn't a meaningful unit-test target beyond confirming it's non-empty/contains the new key mentions if the existing help-text tests already do that for other keys — follow existing precedent, don't invent new test machinery for this.

## Out of Scope

- Batch action wiring (chunk 2/4) and marked-row rendering (chunk 3/4).
- Any change to the "Path: <path>" format or content outside `MODE_SELECT`.

## Further Notes

Chunk 4 of 4 in the multi-select feature (originally `docs/prd/triage/12-multi-select.md`), and the natural last chunk since it documents keys (`space`/`r`/`t` batch scope, marked rendering) that only make sense once chunks 1-3 are in place. Depends on chunk 1/4 (`MODE_SELECT` and mark count in `Model`). Update `Readme.md`'s Controls section as part of this chunk, same as the help text — this is genuinely new user-facing behavior with new keybindings.

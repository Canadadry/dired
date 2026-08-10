---
title: "Multi-select"
description: "Every action in dired today operates on exactly one entry — batch operations (delete/copy/move several files at once) aren't possible."
status: needs-triage
---

> **Draft status:** written from the high-level architecture/roadmap grilling session only — this feature was picked from a list of suggestions and linked conceptually to the async action queue, but never discussed in UX detail. Needs a dedicated grilling session before it's implementation-ready.

## Problem Statement

`selected` in the current code (and the `Model` design from `01-foundation`) is a single index — there's no way to mark several entries and act on all of them at once.

## Solution

Let the user select multiple entries and apply an action (delete/trash/copy/move) to all of them at once, using the action queue from `10-async-action-queue` to run the resulting batch without freezing the UI.

## User Stories

1. As a user, I want to mark several entries for selection, so that I can act on all of them together.
2. As a user, I want to delete/trash multiple selected entries in one action, so that I don't repeat the same action file by file.
3. As a user, I want to copy or move multiple selected entries in one action, so that batch reorganization doesn't take one operation per file.
4. As a developer, I want a batch of actions from a multi-select operation to run through the async action queue (`10`), so that a large batch doesn't block the UI.

## Implementation Decisions

**Decided:**
- Multi-select is conceptually the consumer of `10-async-action-queue`'s queue for bulk operations — deliberately sequenced last in the roadmap, after the queue mechanism exists.

**Not yet decided — needs dedicated grilling (essentially everything about the UX):**
- Keybinding(s) to toggle selection on an entry, select a range, select all/none.
- How a multi-selected entry is visually distinguished in `view()`'s output — the foundation PRD's `STYLE_SELECTED` tag currently means "this is the cursor row"; multi-select needs its own tag (or a combination) distinct from cursor position, since an entry can be marked without being under the cursor, and the cursor can be on an unmarked entry.
- Whether single-entry actions (rename, single delete) still work when multiple entries are marked, or whether marking entries changes what those keybindings do.
- Whether marked entries persist across directory navigation (e.g. can you mark files in one directory, navigate elsewhere, and the marks are lost or kept — almost certainly lost, but not confirmed).

## Testing Decisions

- Not yet decided. The set of selected indices belongs in `Model`; toggling/ranging logic is a plausible table-driven `update()` test target once the exact `Msg` vocabulary for selection is defined.

## Out of Scope

- The queue mechanism itself (`10-async-action-queue`).
- The specific batch operations' individual semantics (`03-copy-move`, `08-trash`).

## Further Notes

Depends on `01-foundation` and `10-async-action-queue`. Sequenced last in the roadmap.

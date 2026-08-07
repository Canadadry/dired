---
title: "Sort entries"
description: "Entries are listed in whatever order readdir() returns them, with no way to sort by name, size, date, or type."
status: needs-triage
---

> **Draft status:** written from the high-level architecture/roadmap grilling session only — this feature was only named ("tri"), never discussed in any detail. Needs a dedicated grilling session before it's implementation-ready; treat everything under Implementation Decisions as placeholder.

## Problem Statement

Entries currently appear in raw `readdir()` order, which is filesystem-dependent and not useful for finding a specific file quickly.

## Solution

Let the user sort the entry list by a chosen key. Exact keys, controls, and defaults are undecided.

## User Stories

1. As a user, I want to sort entries by name, so that I can find a file alphabetically.
2. As a user, I want to sort entries by size, so that I can find the largest/smallest files.
3. As a user, I want to sort entries by modification date, so that I can find recently changed files.
4. As a user, I want directories grouped separately from files (or not), so that navigation matches my expectations — *unconfirmed which behavior is wanted*.

## Implementation Decisions

**Not yet decided — needs dedicated grilling (essentially everything):**
- Which sort keys are supported (name/size/date/type/extension) and in what order they're cycled through.
- Keybinding(s) to change sort key and to toggle ascending/descending.
- Default sort order.
- Whether sort state persists across directory changes / app restarts, or resets every time.
- Whether directories are always grouped before/after files regardless of sort key.
- Where sort logic lives relative to `Model`/`update()` — is the sort key part of `Model`, and is the comparator a pure function tested independently?

## Testing Decisions

- Not yet decided. Expect a pure comparator function to be the natural table-driven test target once the sort keys are defined.

## Out of Scope

- Nothing can be confidently scoped out yet without knowing the feature's actual shape.

## Further Notes

Depends on `01-foundation`. This is one of the least-specified PRDs in the roadmap — placed at position 4 mainly because it's small and independent, not because its design is settled.

---
title: "Restore from trash"
description: "Once files can be moved to ~/.trash (008-trash), there's still no way to bring one back to its original location from inside dired."
status: needs-triage
---

> **Draft status:** written from the high-level architecture/roadmap grilling session only, where this was explicitly deferred to "a later PRD" with almost no discussion beyond its existence and its dependency on `008-trash`'s metadata. Needs a dedicated grilling session before it's implementation-ready; nearly everything below is placeholder.

## Problem Statement

`008-trash` moves deleted items to `~/.trash` with metadata recording their
original location, but nothing in dired reads that metadata back — a
trashed item can only be recovered manually, outside the app.

## Solution

Add a way to browse `~/.trash` from inside dired and restore an item to its
original location using the metadata written by `008-trash`. Exact mechanics
undecided.

## User Stories

1. As a user, I want to browse the contents of `~/.trash` from inside dired, so that I don't have to leave the app to see what I've deleted.
2. As a user, I want to restore a trashed item to its original location, so that I can undo an accidental delete.
3. As a user, I want to be warned if restoring would overwrite something now present at the original location, so that I don't lose the current file.

## Implementation Decisions

**Decided:**
- Depends entirely on `008-trash`'s metadata format (original path per trashed item) being defined first — this PRD cannot be scoped in detail until that format is settled.

**Not yet decided — needs dedicated grilling (essentially everything else):**
- How trash is browsed: a dedicated screen/mode, or trash treated as just another directory to navigate into?
- Keybinding(s) to enter trash browsing and to trigger a restore.
- Collision handling when the original location is now occupied by something else.
- Whether there's a "permanently delete from trash" / "empty trash" action bundled into this PRD or left for later.

## Testing Decisions

- Not yet decided.

## Out of Scope

- Trash retention/auto-emptying policy (not discussed anywhere in the roadmap).

## Further Notes

Depends on `01-foundation` and `008-trash`. This is the least-specified PRD in the roadmap — deliberately deferred by the user during the roadmap-level discussion.

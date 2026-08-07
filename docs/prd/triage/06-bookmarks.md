---
title: "Bookmarks / favorites"
description: "Reaching a frequently-visited directory requires navigating there manually every time, with no way to jump directly."
status: needs-triage
---

> **Draft status:** written from the high-level architecture/roadmap grilling session only — this feature was only named ("favoris") as a pick among several suggestions, with zero further discussion. Needs a dedicated grilling session before it's implementation-ready; almost everything below is placeholder.

## Problem Statement

There's no way to save a directory path for quick return later — every visit means re-navigating from scratch.

## Solution

Let the user save the current directory as a bookmark and jump back to it later. Exact mechanics undecided.

## User Stories

1. As a user, I want to bookmark the current directory, so that I can return to it quickly later.
2. As a user, I want to jump to a bookmarked directory, so that I don't have to navigate there manually.
3. As a user, I want to remove a bookmark I no longer need, so that my bookmark list stays relevant.

## Implementation Decisions

**Not yet decided — needs dedicated grilling (essentially everything):**
- Keybindings to add/remove/jump to a bookmark.
- How a bookmark is selected/jumped to when there are several (a list/picker mode? cycling? numbered slots?).
- Storage: where bookmarks persist (a config file, and in what format) and whether they survive across sessions.
- Whether bookmarks are just paths, or also carry a user-given label.
- Whether this needs a new `Model` mode (e.g. a bookmark-picker screen) similar to the confirm-delete/error states from the foundation PRD.

## Testing Decisions

- Not yet decided. Any pure logic (e.g. "given a list of bookmarks and a jump `Msg`, which path to load") is a table-driven target; reading/writing the bookmark file is I/O and untested, per the same convention as other filesystem effects.

## Out of Scope

- Nothing can be confidently scoped out yet without knowing the feature's actual shape.

## Further Notes

Depends on `01-foundation`. One of the least-specified PRDs in the roadmap.

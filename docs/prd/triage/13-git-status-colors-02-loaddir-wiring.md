---
title: "Git status: loaddir wiring (chunk 2/4)"
description: "The pure gitstatus classifier exists but nothing feeds it real data, so Entry.git_status is never populated from an actual repo."
status: needs-triage
---

## Problem Statement

`gitstatus` (added in chunk 1) can classify entries given porcelain text,
but nothing in `loaddir` calls `git status` or feeds its output through
that classifier. Every `Entry.git_status` is still whatever it defaults
to, regardless of the directory's real git state.

## Solution

After `loaddir` lists entries via `readdir`/`lstat` as it does today, run
`git -C <path> status --porcelain --ignored -uall` via `popen` and, if
that succeeds, hand the output to `gitstatus` (chunk 1) to fill in each
`Entry`'s `git_status` field. This piggybacks entirely on the existing
`CMD_LOAD_DIR` reload cycle — no new `Cmd`/`Msg` types.

## User Stories

1. As a user, I want colors to update automatically after I navigate into
   a different directory, so that I don't have to trigger a manual
   refresh. (Classification updates in this chunk; visible color lands in
   chunks 3/4.)
2. As a user, I want git classification to update automatically after I
   create, delete, rename, paste, or edit a file in the current
   directory, so that the listing never reflects stale git state after an
   action I just took.
3. As a user, when git isn't installed or the current directory isn't
   part of a repository, I want directory loading to simply proceed with
   every entry left unclassified (same as today's rendering), so that a
   missing/inapplicable git integration never surfaces an error or broken
   state.

## Implementation Decisions

- **`loaddir` (modified):** after listing entries via
  `readdir`/`lstat` as it does today, runs
  `git -C <path> status --porcelain --ignored -uall` via `popen`, and —
  if that succeeds — hands the output to `gitstatus` to fill in each
  `Entry`'s `git_status` field. If `popen`/`git` fails for any reason
  (not a repo, git not installed, non-zero exit), every entry is simply
  left at `GIT_STATUS_NONE`; no error is surfaced, matching how a
  non-repo directory already renders today.
- **Recompute triggers:** the git status subprocess runs every time
  `CMD_LOAD_DIR` executes — which already happens on navigation
  (`MSG_GO_PARENT`, entering a directory) and after every successful file
  operation (`MSG_OP_SUCCEEDED` already triggers `CMD_LOAD_DIR` today),
  per `update.c`. No new `Cmd`/`Msg` types are introduced; this piggybacks
  entirely on the existing reload cycle.
- **Performance:** a single `git status --porcelain --ignored -uall`
  subprocess call per directory load/reload (not one call per entry), run
  synchronously and blocking, matching every other filesystem operation
  in this codebase. Accepted as the simplest correct implementation; no
  async/background variant is introduced.

## Testing Decisions

- `loaddir`'s `popen`/subprocess plumbing is not unit tested, matching
  how its existing `readdir`/`lstat` filesystem calls aren't mocked or
  unit tested today — this is the impure I/O boundary, left to
  manual/integration verification.
- Manual verification: load a directory inside a repo with a mix of
  modified/untracked/ignored/conflicted files and confirm (via a debugger
  or temporary print) that `Entry.git_status` matches `git status`'s own
  view; load a non-repo directory and confirm no error surfaces and every
  entry stays `GIT_STATUS_NONE`.

## Out of Scope

- Any rendering/coloring of entries (chunks 3 and 4) — this chunk only
  populates data, nothing observable changes on screen yet.
- An async/background/non-blocking variant of the git status call.
- Special-casing submodules, or descending into a nested repo's own
  `.git` — a dirty submodule is reported by the outer repo's
  `git status` as a single line, same as git's own default behavior.

## Further Notes

This is chunk 2 of 4 for the "Color entries by git status" feature.
Depends on chunk 1 (`gitstatus` module and `Entry.git_status` field).
Produces no visible change on its own — chunks 3 and 4 make this data
show up as color.

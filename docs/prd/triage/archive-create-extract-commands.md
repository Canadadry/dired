---
title: "Archive creation and extraction commands"
description: "dired can browse into an existing .zip/.tar.gz read-only, but has no way to create an archive from files/dirs or extract one to disk, so this PRD is a placeholder pending a dedicated grilling session."
status: needs-triage
---

## Problem Statement

Today dired can enter a `.zip`/`.tar`/`.tar.gz`/etc. archive with →/Enter
and browse its listing read-only, via `execute_list_archive` (`src/dired.c`)
shelling out to `tar -tvf`/`unzip -l`, and preview a single member's
contents via `extract_member_to_fd` shelling out to `tar -xOf`/`unzip -p`.
There is no way to *create* a new archive from selected files/directories,
or to *extract* an existing archive's full contents to disk — a user has to
drop out to a shell for both.

This PRD was raised in passing while planning integration test coverage
(see `docs/prd/triage/integration-test-coverage-per-feature.md`'s Out of
Scope and `integration-test-coverage-cross-feature.md`'s Further Notes,
both of which defer archive-browsing test coverage to this PRD, on the
theory that a working archive-creation command might also double as the
mechanism for materializing archive fixtures in the integration test
harness). It has **not** been grilled yet — this file exists only to
reserve the idea and its rough shape so it isn't lost, not to commit to any
of the specifics below.

## Solution

Rough shape only, all details open: something like `:zip` / `:tar.gz`
command-prompt entries (mirroring the existing `:!` run-shell-command
prompt's style) that create an archive from the current selection (single
entry, or a multi-select marked set — PRD025-028), and a corresponding
extract action for an existing archive entry. Needs a dedicated grilling
session before any of this is treated as decided.

## User Stories

*(placeholder — not yet grilled)*

1. As a user, I want some way to create an archive from one or more files or
   directories without leaving dired, so that I don't have to drop out to a
   shell for a common operation.
2. As a user, I want some way to extract an existing archive's contents to
   disk, so that I don't have to drop out to a shell for the inverse
   operation either.

## Implementation Decisions

None yet — open questions to resolve in the dedicated grilling session
include (non-exhaustive):

- Exact trigger/UI: literal `:zip`/`:tar.gz` typed commands (as the user's
  note suggested), dedicated keybindings, or something else — and how that
  fits alongside the existing `:!` command prompt.
- Which archive formats to support for creation (zip only? the same set
  `ArchiveFormat` already recognizes for browsing — zip, tar, tar.gz/tgz,
  tar.bz2/tbz2, tar.xz/txz, tar.Z?).
- Implementation approach: shell out to `zip`/`tar` (consistent with how
  browsing/extraction already shell out to `unzip`/`tar`), or use a library.
- Source selection: the entry under the cursor only, or integrate with
  multi-select's marked set for creating an archive from multiple entries
  at once.
- Destination/naming: prompted filename, a virtual-line entry (mirroring
  the existing create-file/dir virtual-line UX), or a default derived from
  the source name(s).
- Extraction destination: current directory, a prompted path, or a
  subdirectory named after the archive.
- Collision handling: what happens if the archive name or an extracted
  member's path already exists.
- Whether creation/extraction should be drivable (and thus testable) by the
  pty integration harness at all, or whether — like preview and
  run-shell-command execution — it necessarily forks an interactive
  external process and stays out of harness scope; this directly determines
  whether it can resolve the archive-fixture question the two integration
  test PRDs deferred.
- Error handling/reporting (e.g. via the existing `MODE_ERROR`/`STYLE_ERROR`
  "any key dismisses" convention `001-foundation.md` established).

## Testing Decisions

Not yet determined — depends entirely on the implementation approach chosen
during grilling (in particular whether the operation can run synchronously/
captured like `execute_list_archive` already does, making it pty-harness
testable, versus requiring an interactive external process).

## Out of Scope

Everything not explicitly named above is out of scope until the grilling
session resolves it — this file intentionally commits to as little as
possible.

## Further Notes

- Grill this one from scratch: don't assume anything in this stub survives
  contact with the actual interview, including the `:zip`/`:tar.gz` command
  syntax itself.
- If the eventual design shells out synchronously (like existing archive
  listing does) rather than launching an interactive external process, it
  may retroactively unblock the archive-browsing test scope both
  integration-test PRDs deferred — worth revisiting those two PRDs' Out of
  Scope sections once this one is actually designed.

---
title: "Archive creation and extraction commands"
description: "dired can browse into an existing .zip/.tar.gz archive and copy a single member out, but has no way to create a new archive from files/dirs, or extract an existing archive's full contents to disk."
status: needs-grilling
---

## Problem Statement

Today dired can enter a `.zip`/`.tar`/`.tar.gz`/etc. archive with →/Enter
and browse its listing read-only, and copy a single member out via
yank/paste (`014-read-archive.md`). There is still no way to *create* a
new archive from files/directories, or to *extract* an existing archive's
full contents to disk — both require dropping out to a shell.
But for testing via integration testing we need a way of of creating archive. this is the main purpose of this prd.
## Solution

Four new literal commands typed into the existing `:` command-line
(`MODE_RUN_CMD`, `009-run-shell-command.md`), alongside the existing `!`
prefix: `:zip`, `:tar`, `:tar.gz` to create an archive from the current
selection, and `:extract` to extract the archive under the cursor. No
arguments are typed — each is a bare, exact command word. Do note write twice :

Creating and extracting both hand the terminal to the underlying `zip`/
`tar`/`unzip` process the same way `:!` does — `tb_shutdown()` → fork/exec
→ `wait()` → `tb_init()` — so the tool's own progress output (file-by-file,
like a `tail -f`) streams straight to the real terminal. Unlike `:!`, there
is no `more` pager afterward: the process runs to completion on its own,
then dired immediately redraws with no pause and no "any key" step.

## User Stories

1. As a user, I want to type `:zip`, `:tar`, or `:tar.gz` to archive the
   entry under my cursor, so that I don't have to leave dired to bundle
   files up.
2. As a user, I want the same commands to archive my full multi-select
   marked set into one archive, so that I can bundle several files/dirs at
   once without retyping anything.
3. As a user, I want the archive to always be named `archive.zip` /
   `archive.tar` / `archive.tar.gz`, so that I never have to type a
   filename myself.
4. As a user, I want a name collision (`archive.zip` already exists) to
   auto-resolve to `archive (2).zip`, exactly like paste already does, so
   that a repeat archive never fails or clobbers the previous one.
5. As a user, I want directories in my selection archived recursively, so
   that I don't have to flatten a folder by hand first.
6. As a user, I want to type `:extract` on an archive entry to unpack its
   full contents, so that I don't have to leave dired to get everything
   out at once (as opposed to the existing single-member copy-out).
7. As a user, I want the extracted contents to land in a new subfolder
   named after the archive (`something.zip` → `something/`), so that
   extraction never dumps loose files into my current directory.
8. As a user, I want a subfolder name collision to auto-resolve
   (`something (2)/`), exactly like paste, so that a repeat extraction
   never fails or clobbers a previous one.
9. As a user, I want `:extract` to work on any archive format dired can
   already browse (zip, tar, tar.gz/tgz, tar.bz2/tbz2, tar.xz/txz, tar.Z),
   not just the three creatable formats.
10. As a user, I want `:extract` on a non-archive entry to show the normal
    error screen, so that I get clear feedback instead of silent failure.
11. As a user, I want to see the archive tool's real progress printed to
    the terminal while it runs, so that I know something is happening on
    a large archive, without having to interact with a pager.
12. As a user, I want dired to redraw immediately once the tool exits, with
    no exit-status check and no separate error screen, so that the
    behavior matches `:!`'s existing "hand off, then come straight back"
    model exactly.
13. As a developer, I want the `:`-prefix parsing (which word maps to which
    command) to be a pure, table-tested function, consistent with how the
    `!`-prefix parsing is already tested.
14. As a developer, I want the fork/exec/wait execution itself excluded
    from unit and integration tests, consistent with `execute_run_cmd`/
    `execute_preview`/`execute_launch_editor`.
15. As a developer, I want the integration test fixture builder to gain the
    ability to materialize real `.zip`/`.tar(.gz)` archive files, so that
    the archive-*browsing* test coverage `032`/`033` deferred can finally
    be written (this PRD's own new commands stay untested either way,
    since they're a tty handoff).

## Implementation Decisions

- **Trigger**: extends `MODE_RUN_CMD`'s existing `:` command-line. Today
  only a leading `!` is recognized on `MSG_ACTIVATE`; this adds exact,
  argument-less literal matches for `zip`, `tar`, `tar.gz`, and `extract`.
  No text follows any of these four words — the whole `edit_buf` must
  match exactly.
- **New `Cmd` types**: `CMD_CREATE_ZIP`, `CMD_CREATE_TAR`,
  `CMD_CREATE_TARGZ`, `CMD_EXTRACT_ARCHIVE` — one per command word, matching
  the codebase's existing convention of a distinct `CmdType` per action
  rather than a parameterized generic one (e.g. `CMD_EXTRACT_MEMBER` vs
  `CMD_EXTRACT_MEMBER_TO` are already split this way).
- **Create — source selection**: the marked set (`marked_count > 0`) if
  present, else the single entry under the cursor — same convention as
  existing batch trash/delete/copy/move. Sources are carried in `Cmd`'s
  existing `batch_items[]`/`batch_count` (repurposed to mean "sources",
  with `dest` unused for this Cmd type) when batched, or `path` alone for
  a single source.
- **Create — destination name**: always the literal `archive.zip` /
  `archive.tar` / `archive.tar.gz`, never derived from the source name(s),
  identically for single-entry and multi-select. Collision resolution
  reuses the existing `find_available_name()` helper (` (N)` style,
  e.g. `archive (2).zip`) against the current directory's listing,
  resolved in `update()` before the `Cmd` is built — same pattern paste
  already uses.
- **Create — recursion**: directories in the source set are archived
  recursively (`zip -r`, `tar`'s native directory handling).
- **Extract — target**: the archive entry under the cursor only. Format is
  detected via the existing `archive_format_for_name()` classifier, so
  `:extract` covers the full existing browsing format set (zip, tar,
  tar.gz/tgz, tar.bz2/tbz2, tar.xz/txz, tar.Z) — wider than the three
  creatable formats.
- **Extract — destination**: a new subfolder in the current directory,
  named by stripping the archive's recognized extension (`something.zip`
  → `something/`). Collision resolution reuses `find_available_name()` on
  the subfolder name (`something (2)/`), resolved in `update()` before
  the `Cmd` is built, same as create.
- **Extract — invalid target**: cursor entry not a recognized archive
  format → `MODE_ERROR`, same "not possible"-style error as any other
  operation applied to an entry it doesn't support.
- **Execution model**: `tb_shutdown()` → fork/exec (`execvp`, argv built by
  dired itself — not `sh -c`, since these paths are program data, not
  user-typed shell text, unlike `:!`) with stdout/stderr connected directly
  to the real terminal (no capture, no pipe) → `wait()` → `tb_init()` →
  immediate redraw/reload. No pager, no pause, no "any key" dismissal.
  `zip`/`unzip` are verbose by default; `tar` gets `-v` added for
  equivalent per-file output.
- **No exit-status inspection**: matches `:!`'s existing convention
  exactly. Success or failure, the only feedback is whatever the tool
  printed to the terminal during the handoff; dired always reloads the
  directory afterward with no separate `MODE_ERROR` screen for this
  operation.
- **Argv shape**: `zip -r archive.zip <sources...>`; `tar -cvf archive.tar
  <sources...>`; `tar -czvf archive.tar.gz <sources...>`; extraction uses
  `unzip -d <destdir> <archive>` or `tar -xvf <archive> -C <destdir>`
  depending on detected format.

## Testing Decisions

- **`:`-prefix parsing** (`MODE_RUN_CMD`/`MSG_ACTIVATE` dispatch): pure,
  table-driven unit tests mapping `edit_buf` content to the resulting
  `Cmd` type (or cancel), in the same style and harness as the existing
  `!`-prefix parsing tests from `009-run-shell-command.md`.
- **Name/subfolder derivation and collision resolution**: pure functions
  (archive base name → `archive.<ext>`; archive filename → stripped
  subfolder name), unit tested standalone plus via `update()`'s
  `MSG_ACTIVATE` handling, reusing `find_available_name()`'s existing
  coverage for the collision part.
- **Execution itself** (fork/exec/wait, tty handoff for `zip`/`tar`/
  `unzip`) is **not** unit tested and **not** covered by the pty
  integration harness, matching the existing convention that excludes
  `execute_run_cmd`/`execute_preview`/`execute_launch_editor` — validated
  manually instead.
- **Integration test fixture builder** (`test/integration/fixture.py`)
  gains the ability to materialize real `.zip`/`.tar`/`.tar.gz` fixture
  files (shelling out to `zip`/`tar` directly during fixture setup). This
  unblocks writing integration coverage for existing archive-*browsing*
  behavior (`execute_list_archive`, which is already synchronous/captured
  and was only blocked by the lack of a fixture mechanism) — tracked
  against `032`/`033`'s deferred scope, not part of this PRD's own
  (untestable) create/extract commands.

## Out of Scope

- **Multi-select for `:extract`** — not yet decided whether it extracts
  every marked archive at once or stays cursor-only. Left open.
- **Whether these four commands are blocked while browsing inside a
  nested archive** (via the existing `block_in_archive()` convention used
  by rename/delete/paste/run-command) — not yet decided. Left open.
- **Any archive format beyond zip/tar/tar.gz for creation** (bz2, xz, Z
  compression on create) — extraction covers the full existing format set,
  creation does not.
- **Typed arguments to any of the four commands** (custom name, custom
  destination) — all four are bare, exact-match command words.
- **Exit-status-aware error reporting, a distinct `MODE_ERROR` for this
  operation, or any progress UI beyond the tool's own raw terminal
  output.**
- **Updating `032`/`033`'s own Out of Scope sections** to formally reopen
  archive-browsing test coverage — left as a follow-up once the fixture
  builder change lands.

## Further Notes

Depends on `009-run-shell-command.md` (`MODE_RUN_CMD`/`:` command-line
machinery being extended), `014-read-archive.md` (`ArchiveFormat`/
`archive_format_for_name()`, existing single-member extraction machinery),
`025-028` (multi-select marked set for create's source selection), and the
existing batch-`Cmd` (`batch_items[]`/`batch_count`) and `find_available_name()`
collision machinery already used by copy/move/paste.

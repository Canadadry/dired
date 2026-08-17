---
title: "Archive creation and extraction commands"
description: "dired can browse into an existing .zip/.tar.gz archive and copy a single member out, but has no way to create a new archive from files/dirs, or extract an existing archive's full contents to disk — and the integration suite has no way to build archive fixtures to test the browsing side either."
status: done
---

## Problem Statement

The integration test suite cannot exercise archive-*browsing* behavior
(`execute_list_archive`, single-member extraction) end-to-end, because the
fixture builder has no way to materialize a real `.zip`/`.tar(.gz)` file as
a test fixture — this is exactly what `032`/`033`'s test-coverage PRDs
deferred. The most direct way to unblock that is to give dired itself a way
to create archives, which doubles as the fixture-creation mechanism. That is
this PRD's primary motivation.

Separately, dired can already enter a `.zip`/`.tar`/`.tar.gz`/etc. archive
with →/Enter and browse its listing read-only, and copy a single member out
via yank/paste (`014-read-archive.md`), but has no way to *create* a new
archive from files/directories, or to *extract* an existing archive's full
contents to disk — both require dropping out to a shell. Building this as a
real, usable feature rather than a test-only shim is an expected bonus for
users, even though unblocking test coverage is what's driving it.

## Solution

Four new literal commands typed into the existing `:` command-line
(`MODE_RUN_CMD`, `009-run-shell-command.md`), alongside the existing `!`
prefix: `:zip`, `:tar`, `:tar.gz` to create an archive from the current
selection, and `:extract` to extract the archive(s) under the cursor or in
the marked set. No arguments are typed — each is a bare, exact command word
matched against the full contents of the command line's edit buffer (the
leading `:` is what enters the command-line mode in the first place; it is
not repeated inside the matched text).

Creating and extracting both hand the terminal to the underlying `zip`/
`tar`/`unzip` process the same way `:!` does — suspend the TUI, fork/exec,
wait, resume the TUI — so the tool's own progress output (file-by-file, like
a `tail -f`) streams straight to the real terminal. Unlike `:!`, there is no
pager afterward: the process (or, for batch `:extract`, each process in a
sequential loop) runs to completion on its own, then dired immediately
redraws with no pause and no "any key" step.

`:` itself is already blocked while browsing inside a nested archive, so all
four commands inherit that restriction for free — there's nothing new to
enforce.

## User Stories

1. As a developer, I want the integration test fixture builder to be able to
   materialize real `.zip`/`.tar(.gz)` archive files, so that the
   archive-*browsing* test coverage `032`/`033` deferred can finally be
   written.
2. As a user, I want to type `:zip`, `:tar`, or `:tar.gz` to archive the
   entry under my cursor, so that I don't have to leave dired to bundle
   files up.
3. As a user, I want the same commands to archive my full multi-select
   marked set into one archive, so that I can bundle several files/dirs at
   once without retyping anything.
4. As a user, I want the archive to always be named `archive.zip` /
   `archive.tar` / `archive.tar.gz`, so that I never have to type a
   filename myself.
5. As a user, I want a name collision (`archive.zip` already exists) to
   auto-resolve to `archive (2).zip`, exactly like paste already does, so
   that a repeat archive never fails or clobbers the previous one.
6. As a user, I want directories in my selection archived recursively, so
   that I don't have to flatten a folder by hand first.
7. As a user, I want to type `:extract` on an archive entry under my cursor
   to unpack its full contents, so that I don't have to leave dired to get
   everything out at once.
8. As a user, I want `:extract` to also work against my full multi-select
   marked set, extracting every marked archive in one command, so that I
   can unpack several archives at once without repeating myself.
9. As a user, I want non-archive entries in my marked set to be silently
   ignored by `:extract` rather than blocking the whole batch, so that a
   stray non-archive mark doesn't stop me from extracting the archives I
   actually meant.
10. As a user, I want the extracted contents of each archive to land in its
    own new subfolder named after that archive (`something.zip` →
    `something/`), so that extraction never dumps loose files into my
    current directory or mixes multiple archives' contents together.
11. As a user, I want a subfolder name collision to auto-resolve
    (`something (2)/`), exactly like paste, so that a repeat extraction
    never fails or clobbers a previous one.
12. As a user, I want that auto-resolution to also account for other
    archives extracted in the very same `:extract` batch, so that two
    marked archives that would strip to the same subfolder name (e.g.
    `notes.zip` and `notes.tar.gz`) don't collide with each other.
13. As a user, I want `:extract` to work on any archive format dired can
    already browse (zip, tar, tar.gz/tgz, tar.bz2/tbz2, tar.xz/txz, tar.Z),
    not just the three creatable formats.
14. As a user, I want `:extract` on a cursor entry that isn't a recognized
    archive format (with nothing marked) to show the normal error screen,
    so that I get clear feedback instead of silent failure.
15. As a user, I want to see the archive tool's real progress printed to
    the terminal while it runs, so that I know something is happening on a
    large archive, without having to interact with a pager.
16. As a user, I want dired to redraw immediately once the tool (or, for a
    batch, the last tool in the sequence) exits, with no exit-status check
    and no separate error screen, so that the behavior matches `:!`'s
    existing "hand off, then come straight back" model exactly.
17. As a user, I want these commands to be unavailable while I'm browsing
    inside a nested archive, consistent with every other mutating command,
    so that I'm not surprised by an operation that can't sensibly target
    archive-internal entries.
18. As a developer, I want the `:`-prefix parsing (which word maps to which
    command) to be a pure, table-tested function, consistent with how the
    `!`-prefix parsing is already tested.
19. As a developer, I want the fork/exec/wait execution itself excluded
    from unit and integration tests, consistent with the other TUI-handoff
    commands.

## Implementation Decisions

- **Trigger**: extends the `:` command-line's parsing. Exact, argument-less
  literal matches on the full edit-buffer content for `zip`, `tar`,
  `tar.gz`, and `extract`, checked alongside the existing `!`-prefix check.
  No match falls through to the existing cancel-edit behavior, same as any
  other unrecognized `:` command today.
- **New `Cmd` types**: one distinct type per action (create-zip, create-tar,
  create-tar.gz, extract-archive), matching the codebase's existing
  convention of a distinct `CmdType` per action rather than a parameterized
  generic one.
- **Create — source selection**: the marked set if present, else the single
  cursor entry — same convention as existing batch trash/delete/copy/move.
  Sources are carried in the existing batch-item list (repurposed as
  "sources"; the per-item destination field is unused, since output is
  always the single named archive regardless of source count).
- **Create — destination name**: always the literal `archive.zip` /
  `archive.tar` / `archive.tar.gz`, never derived from source name(s),
  identically for single-entry and multi-select. Collision resolution
  reuses the existing " (N)" collision helper against the current
  directory's listing, resolved before the command is dispatched — same
  pattern paste already uses. Only one destination per create command
  regardless of source count, so no batch-internal collision accumulation
  is needed on the create side.
- **Create — recursion**: directories in the source set are archived
  recursively (`zip -r`; `tar`'s native directory handling).
- **Extract — target selection**: the marked set if present — extracting
  every marked entry that is a recognized archive format, silently skipping
  any marked entry that isn't — else the single cursor entry, which must
  itself be a recognized archive or the operation shows the standard error
  screen.
- **Extract — destination**: one new subfolder per archive, named by
  stripping that archive's recognized extension (`something.zip` →
  `something/`). Collision resolution reuses the existing " (N)" collision
  helper, extended so that batch extraction treats each destination already
  resolved earlier in the same command as taken before resolving the next
  one — a straightforward extension of the existing single-item resolution,
  not a new mechanism.
- **Extract — format coverage**: driven by the existing archive-format
  classifier, so `:extract` covers the full existing browsing format set
  (zip, tar, tar.gz/tgz, tar.bz2/tbz2, tar.xz/txz, tar.Z) — wider than the
  three creatable formats, since the classifier can't distinguish tar's
  compression variants but `tar`'s own extraction autodetects it.
- **Blocked in nested archives**: inherited for free — the `:`
  command-line itself is already unavailable while browsing inside a
  nested archive, so none of the four commands need their own guard.
- **Execution model**: hands the terminal to the child process(es) the
  same way `:!` does (suspend the TUI, fork/exec, wait, resume the TUI,
  redraw) — one process for create; for batch extract, a sequential loop
  of one process per archive, all under a single suspend/resume pair so
  each archive's own progress output streams to the terminal in turn.
  `zip`/`unzip` are verbose by default; `tar` gets `-v` added for
  equivalent per-file output. Unlike `:!`, there is no pipe through a
  pager.
- **No exit-status inspection**: matches `:!`'s existing convention
  exactly — success or failure, the only feedback is whatever the tool
  printed during the handoff; dired always reloads the directory
  afterward with no separate error screen for this operation (the
  "cursor entry isn't an archive" check for single-target extract is a
  pre-flight validation error, not a post-execution one, and is the one
  exception).
- **Argv shape**: `zip -r archive.zip <sources...>`; `tar -cvf archive.tar
  <sources...>`; `tar -czvf archive.tar.gz <sources...>`; extraction uses
  `unzip -d <destdir> <archive>` or `tar -xvf <archive> -C <destdir>`
  depending on detected format. Exact handling of absolute-vs-relative
  source paths in the create argv (and therefore the archive members'
  stored names) is left to implementation.

## Implementation Chunks

1. Name/subfolder derivation and collision-resolution helpers (pure
   functions), including the batch-accumulation extension for extract —
   unit tested standalone first, since every other chunk depends on
   producing correct destinations.
2. `:`-prefix parsing extension (new `Cmd` types, table-driven tests) —
   depends on nothing but the new `Cmd` types existing.
3. Source/destination gathering in the update layer (marked-set-or-cursor
   selection, wiring chunk 1's helpers into the batch-item list) — depends
   on chunks 1 and 2.
4. Archive tool execution (tty handoff, single process for create,
   sequential loop for batch extract) — depends on chunk 3 producing
   correct batch items; not itself unit- or integration-tested, validated
   manually.
5. Integration fixture builder extension (materializing real archives for
   test fixtures) — independent of chunks 1–4; unblocks `032`/`033`'s
   deferred coverage, which is this PRD's primary motivation.

## Testing Decisions

- **`:`-prefix parsing**: pure, table-driven unit tests mapping the edit
  buffer's content to the resulting `Cmd` type (or cancel), in the same
  style and harness as the existing `!`-prefix parsing tests.
- **Name/subfolder derivation and collision resolution**: pure functions
  (archive base name → `archive.<ext>`; archive filename → stripped
  subfolder name; batch-accumulation for extract), unit tested standalone
  plus via the update layer's activation handling, reusing the existing
  collision helper's own coverage for the base collision behavior.
- **Execution itself** (fork/exec/wait, tty handoff for `zip`/`tar`/
  `unzip`) is **not** unit tested and **not** covered by the pty
  integration harness, matching the existing convention that excludes the
  other TUI-handoff commands (shell-command execution, preview, editor
  launch) — validated manually instead.
- **Integration test fixture builder** gains the ability to materialize
  real `.zip`/`.tar`/`.tar.gz` fixture files (shelling out to `zip`/`tar`
  directly during fixture setup). This unblocks writing integration
  coverage for existing archive-*browsing* behavior (already
  synchronous/captured and only blocked by the lack of a fixture
  mechanism) — tracked against `032`/`033`'s deferred scope, not part of
  this PRD's own (untestable) create/extract commands. This fixture-builder
  change is the PRD's actual primary deliverable.

## Out of Scope

- **Any archive format beyond zip/tar/tar.gz for creation** (bz2, xz, Z
  compression on create) — extraction covers the full existing format set,
  creation does not.
- **Typed arguments to any of the four commands** (custom name, custom
  destination) — all four are bare, exact-match command words.
- **Exit-status-aware error reporting, a distinct error screen for
  post-execution failure, or any progress UI beyond the tool's own raw
  terminal output.**
- **Updating `032`/`033`'s own Out of Scope sections** to formally reopen
  archive-browsing test coverage — left as a follow-up once the fixture
  builder change lands.
- **Absolute-vs-relative source path handling** in the create argv — an
  implementation detail, not specified here.

## Further Notes

Depends on `009-run-shell-command.md` (`MODE_RUN_CMD`/`:` command-line
machinery being extended), `014-read-archive.md` (archive format
classifier, existing single-member extraction machinery), `025-028`
(multi-select marked set for source selection), and the existing batch-
command and " (N)" collision-resolution machinery already used by
copy/move/paste.

This PRD replaces the earlier `needs-triage` placeholder stub that
previously lived at this slug (which only reserved the idea pending a
dedicated grilling session) and the working `needs-grilling` draft it was
merged from.

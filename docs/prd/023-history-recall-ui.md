---
title: "Command history recall in the `:` prompt"
description: "Typing a `:!command` in dired has no memory of commands run before, forcing users to retype the same ad-hoc shell commands over and over in the same folder."
status: done
---

## Problem Statement

`009-run-shell-command.md` added the `:` command prompt (press `:`, type `!some command`, press Enter, dired shells out and pages the output) but explicitly deferred any memory of past commands: *"No command history/recall either — every entry into command mode starts with an empty buffer."* In practice, users re-run the same handful of commands in a given folder over and over (`!git status`, `!make test`, `!ls -la`) and have to retype each one from scratch every time, with no way to recall or cycle through what they've already run there.

`history-storage-data-model.md` (PRD1) builds the underlying storage — a disk-persisted, per-folder hashmap of recent commands — but is deliberately a standalone data/persistence library with no knowledge of dired's `Model`/`Msg`/`Cmd`/`update()` machinery. This PRD is the wiring: making the `:` prompt actually read from and write to that storage.

## Solution

While composing a `:!command`, pressing Up and Down cycles backward and forward through that folder's previously-run commands (most recent first), exactly like shell history recall. Every command actually run gets recorded against the folder it ran in, deduplicated so re-running an existing command moves it to most-recent instead of appearing twice. History loads once when dired starts and persists to disk as commands run, so it survives restarts. Deleting a folder (via dired's own delete/trash) also drops that folder's history, and a startup pass prunes history for any folder that no longer exists on disk (e.g. deleted outside of dired).

## User Stories

1. As a user, I want to press Up while composing a `:!command`, so that the most recently run command in this folder appears without retyping it.
2. As a user, I want to keep pressing Up to walk further back through older commands run in this folder, so that I can find one from a few commands ago.
3. As a user, I want Up to stop at the oldest recorded command instead of wrapping back around to the newest, so that I don't lose track of where I am in the history.
4. As a user, I want to press Down to walk back toward more recent commands after pressing Up, so that I can undo going too far back.
5. As a user, I want whatever I'd already started typing before pressing Up to reappear if I press Down past the most recent recalled command, so that I don't lose my own in-progress command just because I glanced at history.
6. As a user, I want to be able to edit a recalled command (append characters, backspace) before running it, so that I can tweak a past command instead of retyping it entirely.
7. As a user, I want pressing Up or Down again after editing a recalled command to jump to a different history entry (not preserve my edit), so that history navigation behaves predictably, the same as it does in a normal shell.
8. As a user, I want Up/Down to do nothing (not error, not crash) when a folder has no history yet, so that a brand-new folder behaves exactly like today before this feature existed.
9. As a user, I want every command I actually run (even ones I've run before) to be remembered, so that my most-used commands are always easy to recall later.
10. As a user, I want re-running a command that's already in this folder's history to just move it to "most recent" instead of cluttering my history with duplicates, so that cycling through history doesn't show the same command over and over.
11. As a user, I want my command history to still be there the next time I open dired in the same folder, so that history isn't wiped out every time I close the app.
12. As a user, I want history recall to only apply to the `:` command prompt, not the filter or glob search prompts, so that Up/Down keeps behaving the way it always has everywhere else.
13. As a developer, I want dired to load the on-disk history once at startup rather than on every `:` press, so that composing a command never incurs disk I/O just to check history.
14. As a developer, I want a startup pass that drops history for any folder that no longer exists on disk, so that history for folders deleted outside of dired (e.g. via another terminal) doesn't accumulate forever.
15. As a user, I want deleting or trashing a directory in dired to also remove that directory's command history, so that history for a folder I've deliberately gotten rid of doesn't linger.
16. As a developer, I want a failing command (non-zero exit status) to still be recorded in history, consistent with `009-run-shell-command.md`'s existing "no exit-status inspection" behavior, so that history recording doesn't introduce a new kind of special-casing this feature didn't need.
17. As a developer, I want the `:` key's existing behavior (open the prompt with an empty buffer) to stay exactly as-is when a folder has history, so that this feature is purely additive to `009-run-shell-command.md`, not a redesign of it.
18. As a user, I want to see in the help bar that Up/Down recall history in the command prompt, so that I can discover the feature without reading documentation.

## Implementation Decisions

- **Scope**: only `MODE_RUN_CMD` (the `:!command` prompt from `009-run-shell-command.md`) gets history recall. The filter (`f`/`F`) and glob (`g`/`G`) prompts, which share the same text-entry key-handling path, are untouched — they're search patterns, not commands, and PRD 009 is specifically what deferred "command history."

- **Loading**: the hashmap-of-arenas built in PRD1 is loaded from disk exactly once, at dired startup — alongside the existing one-time startup initialization (`load_preview_config()`), before the main event loop begins. It is *not* reloaded on every `:` press. Immediately after loading, a startup sweep walks every occupied folder entry in the loaded hashmap and removes (via PRD1's delete-by-path, which also persists the removal) any whose folder no longer exists on disk.

- **`Model` changes**:
  - A new field holds the loaded hashmap-of-arenas for the whole process lifetime as a single pointer (not pointer-to-pointer) — set once at startup, read (but never reassigned) for the rest of the run.
  - A new cursor field tracks position within the *current* folder's history while cycling: a sentinel value means "not currently cycling" (the buffer holds either nothing or a not-yet-cycled-from draft); any other value is a position in the current folder's arena, oldest at one end, most-recent at the other.
  - A new stash (buffer + length) holds whatever was typed before cycling began, captured the moment the first Up is pressed, and restored if Down is pressed past the most-recent recalled entry.
  - Entering `MODE_RUN_CMD` (still triggered by `:`, unchanged from PRD 009) resets the cursor to "not cycling," consistent with it already resetting the edit buffer to empty.

- **Key handling**: Up/Down arrows, which are currently unhandled (produce no message) in every text-entry mode including `MODE_RUN_CMD`, gain a new case: when the current mode is specifically `MODE_RUN_CMD`, Up produces a new "recall previous" message and Down produces a new "recall next" message. Every other text-entry mode (Rename, Create, Filter, Glob) keeps arrows unhandled exactly as today — this is not a general text-entry behavior change.

- **Cycling behavior**:
  - First "recall previous" in a session (cursor at the "not cycling" sentinel): look up the current folder in the hashmap (a fast in-memory read — the hashmap is already loaded, so this touches no disk). If the folder has no history, do nothing. Otherwise: stash the current buffer contents, move the cursor to the most-recent entry, and replace the buffer with that command's text. The arena stores command text without its leading `!` (that's stripped before the command actually runs); the prompt always expects the `!` prefix, so it's re-added when populating the buffer from a recalled entry.
  - Subsequent "recall previous": move the cursor one step further back (older), **clamped** at the oldest entry — pressing Up at the oldest entry does nothing further, it does not wrap around to the newest.
  - "Recall next": move the cursor one step forward (more recent). Pressing it again once already at the most-recent entry restores the original stashed buffer and resets the cursor to "not cycling" — clamped at that boundary, no wraparound back to the oldest.
  - A recalled command replaces the buffer's contents wholesale each time. This does not add cursor-movement editing to the buffer — it remains append-at-end/backspace-at-end-only, exactly as PRD 009 established for Rename/Create/Filter/Glob/Run-Command alike.
  - Typing or backspacing after a recall edits the buffer normally in place; this edit is never written back into the stash. Pressing Up/Down again from an edited-but-not-yet-run recalled command jumps to a different history entry, discarding the in-place edit — matching ordinary shell history behavior.

- **Recording a run**: the existing command-execution path (already the one place in this codebase that shells out to run a `:!command`, already receiving the folder and the already-`!`-stripped command text) is extended so that once the shell command and its pager have both finished, the run is recorded against that folder using PRD1's push/dedup arena operation, then persisted to disk via PRD1's write-one-folder-slot (plus PRD1's header-rewrite path if this was that folder's very first history entry). This happens unconditionally — a failing command (non-zero exit) is recorded exactly the same as a successful one, matching PRD 009's existing "no exit-status inspection" convention.

- **Folder-delete cleanup**: when a directory delete or trash operation succeeds (the directory-vs-file distinction is already known at the point these operations are dispatched), that path's hashmap entry is removed via PRD1's delete-by-path, which also persists the removal to disk. Deleting a file never touches history.

- **Help bar**: the existing help text gains a short addition noting that Up/Down recall history while composing a `:` command, mirroring how PRD 009 added its own segment there.

- **No behavior change when a folder has no history**: pressing `:` opens the prompt with an empty buffer exactly as before; Up/Down simply have no effect until at least one command has ever been run in that folder.

## Testing Decisions

- Good tests here assert on `update()`'s returned `Model` (buffer contents, cursor position) and, separately, on real hashmap/arena state via PRD1's exposed API — never on terminal output or by shelling out to a real `sh`/`more`, consistent with this codebase's established testing conventions.
- **Recall cursor logic** (the new Up/Down message handling in the edit-mode update path): table-driven, using a small in-memory fixture built directly with PRD1's API (a folder with a known, ordered list of commands) rather than loading anything from disk. Cases: first Up recalls the most-recent entry and stashes the draft; repeated Up walks older and clamps at the oldest; Down walks back toward newest; Down past the newest restores the original stashed draft and resets to "not cycling"; Up/Down on a folder with zero history entries is a no-op; a recalled entry's leading `!` is correctly re-added to the buffer. Prior art: PRD 009's own table-driven coverage of `MODE_RUN_CMD`'s edit-mode handling, and the general `update()`-output-assertion style used throughout `test/update_test.c`.
- **Recording, folder-delete cleanup, and the startup sweep** are impure (they touch the real hashmap/disk via PRD1's API, or in the recording case also fork/exec a real shell) and are **not** unit tested, consistent with the existing, already-established exclusion of `execute_run_cmd`'s fork/exec/pipe/wait behavior from unit tests. Validated manually instead: running the same command twice in a folder and confirming it appears once, most-recent; deleting a folder with history and confirming its entries are gone from a subsequent load; deleting a folder's contents outside of dired (a plain `rm -rf` from another terminal) and confirming its history is pruned the next time dired starts; restarting dired and confirming previously-run commands are still recallable.

## Out of Scope

- **Filter (`f`/`F`) and glob (`g`/`G`) history** — explicitly not covered; only `MODE_RUN_CMD` gets recall.
- **Cursor-movement editing within the command buffer** — still out of scope, per PRD 009's own out-of-scope note; unaffected by this PRD.
- **The hashmap/arena's internal byte layout and on-disk format** — entirely PRD1's contract, consumed here only through its exposed API (lookup, push/dedup, delete-by-path, load-all).
- **File locking or multi-process consistency** — a second concurrently running dired session in the same folder won't see commands run in the first session until it's restarted; this is PRD1's stated, accepted limitation, not addressed here.
- **Reloading history mid-session** (e.g. a manual refresh key) — history is loaded exactly once, at startup; no mechanism is added to re-read the disk file while dired is running.
- **Any UI beyond the existing `:` prompt line** — no new panel, no separate history browser/picker; recall is strictly Up/Down cycling through the same single-line prompt PRD 009 already built.

## Further Notes

Depends on `009-run-shell-command.md` (the `MODE_RUN_CMD` prompt this feature adds recall to) and `history-storage-data-model.md` (PRD1, the storage this feature is entirely built on top of — that PRD must land first). This PRD adds no new storage design of its own; every decision here is about *when* and *how* dired's existing `update()`/`Msg`/`Cmd` flow calls into PRD1's already-designed API.

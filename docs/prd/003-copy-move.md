---
title: "Copy / move files and directories"
description: "dired has no way to copy or move a file or directory today — the only way to relocate something is to leave the app."
status: done
---

## Problem Statement

The current feature set covers navigate, open (vim), rename, create
file/directory, delete, and preview — but not copy or move. This is the
largest functional gap in dired relative to a basic file manager: relocating
or duplicating something still means dropping out to a shell.

## Solution

Add a yank/paste-style copy and move flow, implemented as synchronous
effects within the `Cmd` mechanism established in the foundation PRD — no
queue, no async, no progress bar. Those are deliberately deferred to the
async-action-queue PRD (item 9), which later upgrades this PRD's `Cmd`s to
run through the queue instead.

The flow: press a key to "yank" the selected entry with copy or move intent,
navigate dired anywhere (including into subdirectories or back to the
parent), then press paste to copy/move the yanked entry into whatever
directory is currently open. The actual file operations are delegated to
the system `cp -r` / `mv` binaries via `fork`/`execvp`, run synchronously
and blocking, rather than reimplemented with raw syscalls — this gets
correct recursive copy and cross-filesystem move (`mv`'s own `EXDEV`
fallback) for free.

## User Stories

1. As a user, I want to copy the selected file to another directory, so that I can duplicate it without leaving dired.
2. As a user, I want to copy the selected file within the same directory, so that I get a numbered duplicate (e.g. `notes (1).txt`) without typing a new name.
3. As a user, I want to move the selected file or directory to another location, so that I can reorganize my filesystem.
4. As a user, I want to copy an entire directory and have its full contents come with it, so that copying a folder behaves the way it does in any other file manager.
5. As a user, I want to move a file across filesystems/mount points and have it just work, so that I don't need to know or care about `EXDEV` semantics.
6. As a user, I want a copy or move that would overwrite an existing file to instead land as a numbered duplicate, so that I never lose data to a silent overwrite.
7. As a user, I want distinct keys for "yank to copy," "yank to move," and "paste," so that intent is set once at yank time and I don't have to remember it at paste time.
8. As a user, I want to see that I have something pending to paste while I'm navigating, so that I don't lose track of an in-flight copy/move.
9. As a user, I want yanking a second item to simply replace the first, so that I never have to explicitly clear a stale yank before starting a new one.
10. As a user, I want paste to always target the directory I'm currently browsing, so that "where does this land" is never ambiguous.
11. As a user, I want yanking `.`/`..` to be refused, so that I can't accidentally start a copy/move of the current or parent directory itself.
12. As a user, I want a failed copy to clean up after itself, so that a permission error partway through a large directory doesn't leave a half-copied mess with no other trace.
13. As a user, I want a failed move to never delete anything on my behalf, so that a failure mode never has a chance of destroying the only remaining copy of my data.
14. As a developer, I want copy/move expressed as `Cmd` variants executed synchronously by `main()`, matching how rename/delete already work, so that no new execution mechanism is needed for this PRD.
15. As a developer, I want the collision-avoidance naming logic to be a pure, table-driven-testable function, so that its numbering behavior can be verified without touching a real filesystem.
16. As a developer, I want the yank/paste state machine covered by the same table-driven `update()` tests already used for navigation and rename, so that its edge cases (no pending yank, re-yank replaces, protected-name guard) are regression-proof.
17. As a developer, I want `cp`/`mv`/`rm` invoked via `execvp` with an argv array, never via a shell string, so that a maliciously or accidentally named file can never inject a shell command.

## Implementation Decisions

**Model (`model.h`):**
- Add yank state to `Model`, independent of `AppMode` — a pending yank must survive normal navigation (`MSG_MOVE_*`, `MSG_GO_PARENT`, `MSG_ACTIVATE`, `MSG_DIR_LOADED`), which never changes `mode` away from `MODE_NAV`. Concretely: a source path buffer plus a copy-vs-move flag; an empty path is the "no pending yank" sentinel, consistent with how `edit_buf` already uses an empty string as its default state.
- No change to `AppMode` — yanking does not enter a new mode. This is a deliberate difference from rename/create/delete, which are all full-screen modal edits; yank/paste is meant to be transparent to normal browsing.

**Messages (`msg.h`) and keybindings:**
- Add `MSG_YANK_COPY`, `MSG_YANK_MOVE`, `MSG_PASTE`.
- Bind `c` → yank-copy, `m` → yank-move, `p` → paste (alongside existing `r`/`f`/`d`/`space`/`q`).
- No cancel key for a pending yank: `MSG_CANCEL` (Esc) is not extended to clear it. A pending yank only ever ends by being pasted or replaced by a new yank.

**Commands (`cmd.h`):**
- Add `CMD_COPY`, `CMD_MOVE`, reusing the existing `path` (source) / `path2` (destination) fields — no new fields needed. `cmd->is_dir` is not used by either variant: `cp -r` is safe to run unconditionally (recursion is a no-op on a plain file), and `mv` never takes a recursion flag.

**`update.c` (`handle_nav`):**
- `MSG_YANK_COPY`/`MSG_YANK_MOVE`: guarded by `is_protected_name()` exactly like rename/delete already are (update.c:50-51, 64-65). Sets the yank source path + copy/move flag on `out_model`; does not touch `mode`. A second yank while one is pending silently overwrites it (last yank wins, single-slot clipboard).
- `MSG_PASTE`: no-op if no yank is pending. Otherwise: builds a candidate destination as `join(current_path, basename(yank_path))`, resolves it against `out_model->entries` using the new naming-collision helper (below) to get a free name, and emits `CMD_COPY` or `CMD_MOVE` (per the stored flag) with `path` = yank source, `path2` = resolved destination. The yank state is cleared immediately when paste fires — clearing is tied to the paste *action*, not to the later `MSG_OP_SUCCEEDED`/`MSG_OP_FAILED` outcome, so a failed paste requires re-yanking to retry (kept simple, matches the "clears after paste" decision literally).

**`helpers.c`/`helpers.h` — new deep module:**
- A pure function, e.g. `void find_available_name(const char *base_name, const Entry *entries, int entry_count, char *out_name, size_t out_size)`, implementing the single collision-avoidance algorithm used everywhere a paste can collide (same-directory duplicate and cross-directory collision are the *same* case, not two): if `base_name` doesn't collide, use it as-is; otherwise insert `(1)`, `(2)`, ... before the extension (`file.txt` → `file (1).txt` → `file (2).txt`; directories have no extension to preserve — `notes` → `notes (1)`) and return the first name not present in `entries`. No "copy" wording anywhere.
- This is the standout deep module for this PRD: zero I/O, a single well-defined contract, reused by both the same-directory-duplicate and cross-directory-collision stories.

**`dired.c` (`execute_cmd`):**
- `execute_copy(src, dst)`: `fork` + `execvp("cp", {"cp", "-r", src, dst, NULL})`, blocking `waitpid`. On non-zero exit: delete whatever partial result ended up at `dst` via `fork` + `execvp("rm", {"rm", "-rf", dst, NULL})` (a recursive delete the app has no other need for, since `execute_delete` only ever removes one already-empty-checked entry), then return `MSG_OP_FAILED`.
- `execute_move(src, dst)`: `fork` + `execvp("mv", {"mv", src, dst, NULL})`, blocking `waitpid`. On non-zero exit: return `MSG_OP_FAILED` directly, with **no** cleanup of `dst` — asymmetric with copy on purpose. `mv`'s cross-filesystem fallback only removes the source after confirming the destination copy succeeded, so on failure the source should be intact and only the destination might be partial; since that ordering guarantee lives inside `mv` rather than our own code, deleting `dst` on a move failure risks destroying the only remaining copy of the user's data if that guarantee is ever wrong. Copy has no such risk because the source is never touched.
- Both children run non-interactively (unlike `execute_launch_editor`/`execute_preview`, which call `tb_shutdown()`/`tb_init()` around vim/`more`): `cp`/`mv`/`rm` never need the terminal. Their stderr is captured (piped, not inherited) so a real OS error (permission denied, disk full, etc.) can be folded into the `MSG_OP_FAILED` error message the same way `strerror(errno)` is used elsewhere — a bare exit code alone isn't a useful error message. Stdout is discarded; none of `cp -r`/`mv`/`rm -rf` print anything in the non-verbose invocation used here.
- All three use `execvp` with an explicit argv array — never `system()`/`popen()` — so no shell ever interprets a filename.

**`view.c`:**
- `add_prompt_line()`'s `MODE_NAV` case (currently always a blank line, view.c:40-42) needs to become a function of yank state too: render something like `Yanked: <name> (move)` when a yank is pending, blank otherwise. This is a real gap in the current architecture — the prompt line today is driven purely by `mode`, and yank state deliberately lives outside `mode`.
- `HELP_TEXT` gains the three new keys (`c`: Yank copy, `m`: Yank move, `p`: Paste).

## Testing Decisions

- `find_available_name()` is a pure function with no I/O — table-driven unit tests (tier 1), same style as `test_is_protected_name`/`test_mode_to_str` in `test/helpers_test.c`: no collision, one collision, multiple sequential collisions, directory (no extension) case, empty-entries case.
- The `update()` extensions (yank guarded by protected-name check, yank not touching `mode`, re-yank replacing the pending one, paste with nothing pending being a no-op, paste producing the right `Cmd` type/paths, paste clearing yank state) are table-driven logic tests (tier 2), same style and harness as `test_move_selection` in `test/update_test.c` — construct a `Model`, feed a `Msg`, assert on the resulting `Model`/`Cmd`.
- `execute_copy`/`execute_move` in `dired.c` are I/O plus process-exec and follow the existing tier-3 pattern: not unit tested in isolation, verified instead via temp-dir integration tests (`mkdtemp`, real files, assert resulting filesystem state) — the same pattern already used for `load_directory` and proposed for delete/rename in `docs/TESTING.md`.

## Out of Scope

- Async/queued execution, progress bar, or any "working..." indicator during a blocking copy (item 9). Accepted for v1: the UI blocks for however long `cp`/`mv` takes, with no feedback.
- Multi-file copy/move (item 10, multi-select) — yank operates on exactly one entry.
- Cancelling a pending yank. Once yanked, an item stays pending until pasted or replaced by a new yank; there is no explicit clear.

## Further Notes

Depends on `01-foundation`. Sequenced third because the file-preview PRD was
judged higher value by the user, even though copy/move fills a larger
functional gap.

This PRD deliberately breaks from the "raw syscall per `Cmd`" pattern used
by rename/create/delete: recursive copy and cross-filesystem move are both
already correctly solved by the system `cp`/`mv` binaries, and reimplementing
either (recursive directory walk with mid-walk error handling; `EXDEV`
detection and copy+delete fallback) would be substantial, easy-to-get-wrong
scope this PRD doesn't need to take on.

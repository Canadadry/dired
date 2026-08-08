---
title: "File preview (external pager)"
description: "Navigating dired gives no indication of a file's content until you open it in vim, so choosing the right file among several similarly-named ones means opening each one."
status: done
---

## Problem Statement

Today, the only way to see what's inside a file is to open it in vim, which
takes over the whole screen and requires quitting back out to keep browsing.
There's no quick way to glance at a file's content while navigating.

## Solution

Add a "preview" key that, on the currently-selected regular file, shells out
to the system `more` pager exactly the way dired already shells out to vim
to edit a file — same `tb_shutdown()` → fork/exec/wait → `tb_init()`
pattern, same return-to-the-browser-afterward flow. No in-app preview
panel, no split layout, no custom pager logic: `more`'s own default
keybindings (space/enter to page, `q` to quit, etc.) apply as-is.

This directly resolves the cross-PRD flag `001-foundation.md` raised
against this PRD: foundation settled on "no panels — `view()` stays a flat
list of styled lines" as a roadmap-wide principle, and asked this PRD to
revisit its multi-panel assumption before leaving triage. It's revisited:
there is no panel, no split, and no rework of `view()`'s output type.
`view()`/`Model` are essentially untouched by this feature.

## User Stories

1. As a user, I want to press a key to preview the selected file's content, so that I can identify the right file without opening it in vim for editing.
2. As a user, I want the preview to open in a familiar pager (`more`) with its normal keybindings, so that I don't have to learn a new set of controls just for dired.
3. As a user, I want pressing the preview key on a directory to do nothing, so that I don't get a confusing error trying to page a directory's contents.
4. As a user, I want a binary file to be rejected with a clear in-app error message instead of being dumped raw into my terminal, so that I don't get garbled/control-character output on screen.
5. As a user, I want the file listing to return to view, refreshed, after I quit the pager, so that browsing continues exactly where it did after editing a file in vim.
6. As a developer, I want the preview key handled through the same `Msg`/`Cmd`/`update()` flow as every other action, so that no new `Model` state or `AppMode` is introduced just for this feature.
7. As a developer, I want success/failure after running the pager to reuse the existing `MSG_OP_SUCCEEDED`/`MSG_OP_FAILED` handling (reload directory / show `MODE_ERROR`), so that this feature doesn't duplicate logic that already exists for the editor-launch and file-op commands.
8. As a developer, I want binary detection extracted as a small, pure, table-driven-testable function, so that a future "hex view for binary files" PRD can reuse the same detection instead of re-deriving it.
9. As a developer, I want the help bar updated to mention the new preview key, so that the key is discoverable the same way existing keys are.

## Implementation Decisions

- **New `Msg`**: `MSG_PREVIEW`, produced only outside text-entry modes (same guard as `MSG_RENAME`/`MSG_NEW_FILE`/`MSG_NEW_DIR`/`MSG_QUIT`). Bound to the space key (`ev.ch == ' '`) in `translate_event`'s nav-mode key table.
- **New `Cmd`**: `CMD_PREVIEW`, carrying just `path` (no `path2`, no `is_dir` — only ever issued for regular files).
- **`update.c` / `handle_nav`**: on `MSG_PREVIEW`, if the selection is out of range or the selected entry is not `S_ISREG`, no-op (covers both "nothing selected" and "directory selected" — directory check is pure, using the `st.st_mode` already loaded into `Entry`, no I/O needed to decide it). Otherwise, `out_cmd->type = CMD_PREVIEW` with the joined `current_path`/entry-name path. No other change to `update()` — the generic `MSG_OP_SUCCEEDED` (reload current dir) and `MSG_OP_FAILED` (enter `MODE_ERROR`, any key dismisses) handling at the top of `update()` already covers this `Cmd`'s outcome, same as it does for `CMD_LAUNCH_EDITOR` and every file op.
- **Binary detection — pure helper**: `int is_binary_content(const unsigned char *buf, size_t len)` in `helpers.c`/`helpers.h`, alongside `is_protected_name`/`mode_to_str`. Returns true if any byte in `buf[0..len)` is `0x00`. No knowledge of files or paths — pure buffer-in, bool-out.
- **`dired.c` / `execute_preview(path)`**: opens `path`, reads up to 512 bytes, calls `is_binary_content` on what was read.
  - Binary → `msg_failed("preview: binary file")` (or similar), same `msg_failed` helper already used by the other `execute_*` functions.
  - Text (or read error handled via the normal `fopen`/`errno` failure path, same as other ops) → `tb_shutdown()`, fork/exec `more path` (`execlp("more", "more", path, (char *)NULL)`), `wait(NULL)`, `tb_init()`, return `MSG_OP_SUCCEEDED` — identical shape to `execute_launch_editor`, just a different program and no editing implied.
  - Note the 512-byte read for detection and the subsequent full-file read by `more` are separate: dired never buffers file content into `Model`, `more` reads the file itself from disk.
- **Empty file**: no special-casing — `is_binary_content` on a zero-length read returns false (not binary), so `more` is launched and does whatever `more` does by default with an empty file.
- **Help bar**: `HELP_TEXT` in `view.c` gains a `"space: Preview"` segment.
- **No new `AppMode`, no new `Model` fields.** The feature is entirely: one new `Msg`, one new `Cmd`, one new `handle_nav` case, one new `execute_*` function, one pure helper, one key binding, one help-text edit.

## Testing Decisions

- Good tests here assert on `update()`'s and pure helpers' return values, never on terminal output, real filesystem state, or by shelling out to `more` — consistent with `001-foundation.md`'s testing conventions.
- **`handle_nav`'s `MSG_PREVIEW` case** (via `update()`): table-driven, one case per `{Model with selection = regular file / directory / out-of-range} + MSG_PREVIEW -> expected Cmd}` — regular file yields `CMD_PREVIEW` with the correct joined path, directory and out-of-range both yield `CMD_NONE`. Prior art: the existing `update()` table-driven tests from foundation.
- **`is_binary_content`**: table-driven, cases for empty buffer, all-printable text, a null byte at the start/middle/end, and a buffer at the 512-byte boundary. Prior art: `is_protected_name`/`mode_to_str` pure-helper tests from foundation.
- **`execute_preview`** (file I/O + fork/exec/wait) is explicitly **not** unit tested, same convention as `execute_launch_editor` and every other `execute_*` function in `dired.c` — validated manually by running the app.
- No scripted terminal integration tests for launching `more` — same "manual validation only" convention foundation established for the vim launch.

## Out of Scope

- Any in-app pager/scrolling logic, side-by-side or top/bottom split panels, or rework of `view()`'s output type — explicitly dropped in favor of shelling out to `more`.
- A hex/binary viewer — `is_binary_content` is deliberately extracted as a small reusable pure function so a future PRD can build this on top, but no viewer is built now; binary files just get an error message.
- Making the pager configurable (e.g. `less` instead of `more`, or reading `$PAGER`) — hardcoded to `more` for this PRD.
- Previewing directories — no-op, not a mini-listing.
- Copy/move (deferred to its own PRD, sequenced after this one, unchanged from the original draft).

## Further Notes

Depends on `001-foundation` (Model/Msg/Cmd, `update`/`view` split), already shipped (commit `82b2b57`). This revision replaces the original draft's live, in-app, side-by-side/top-bottom preview-panel concept — which foundation had flagged as conflicting with its "no panels" roadmap-wide principle — with a manual, external-pager launch that reuses the existing editor-launch `Cmd` pattern almost verbatim. As a result this is now one of the smallest PRDs in the roadmap: no new `Model` state, no new `AppMode`, no `view()` changes beyond the help-bar string.

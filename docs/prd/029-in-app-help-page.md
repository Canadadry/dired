---
title: "In-app help page (h key)"
description: "The only way to see the full key-binding list is the -help CLI flag (before dired starts) or a cramped, truncated one-line footer while running; there's no way to review the controls from inside a running session."
status: done
---

## Problem Statement

dired's key bindings are currently documented in three independently hand-maintained places that already drift from each other in wording:

1. `print_help()` in `src/dired.c` — printed by the `-help` CLI flag, before dired starts.
2. `HELP_TEXT` in `src/view.c` — a single-line, comma-separated footer always rendered on the last row of the running app.
3. The "Controls" block in `Readme.md`.

Once dired is already running, the only in-session reference is the footer, which is too small to read comfortably and gets truncated on narrow terminals. A user who forgets a binding (e.g. how to glob recursively, or how selection-mode range-select works) has to quit and rerun `dired -help`, or shell out, to look it up.

## Solution

Add an `h` key binding (unbound today, in both `MODE_NAV` and `MODE_SELECT`) that opens the same keybinding reference the `-help` CLI flag prints, paged through `less -R` without leaving the running app — the same way previewing a text file with `space` already works. The user quits `less` with its own `q` to return to dired exactly where they left off.

The three drifting copies are collapsed into one: a single static `HELP_CONTENT` text block becomes the shared source of truth for both `print_help()` and the new in-app `h` page. The old one-line `HELP_TEXT` footer is removed entirely, since the full page now serves that purpose. `Readme.md`'s Controls block is left as-is (out of scope for this PRD).

## User Stories

1. As a dired user mid-session, I want to press `h` to see the full key-binding list, so that I don't have to quit and rerun `dired -help` to look something up.
2. As a dired user, I want the in-app help page to be pageable/scrollable, so that the full list is readable even on a short terminal, unlike the truncated one-line footer.
3. As a dired user, I want to dismiss the help page the same way I dismiss a file preview (quit `less`), so that the interaction is consistent with something I already know.
4. As a dired user running `dired -help` from the shell, I want the CLI output and the in-app `h` page to show the same wording, so that I don't have to reconcile two different descriptions of the same key.
5. As a maintainer, I want the key-binding descriptions to live in one place, so that a wording change or new binding doesn't need to be kept in sync across multiple copies by hand.
6. As a dired user, I want pressing `h` to have no effect while marks/selection-mode UI is mid-interaction in a way that would be surprising, so it's fine that `h` simply does nothing while `MODE_SELECT` doesn't explicitly handle it (no crash, no mode change).
7. As a dired user, I want the CLI `-help` output to stop showing a `Usage: dired [-help]` line, since that line only explains how to print help — which is redundant once help is already printing.
8. As a dired user, I want `Build time:` and `Detected window size:` to remain in the CLI `-help` output, since those are useful runtime/debug facts specific to the CLI invocation, not part of the key-binding reference itself.

## Implementation Decisions

- **Shared help content**: introduce one static `HELP_CONTENT` string constant (title line + the full controls list), reusing `print_help()`'s current, more-descriptive wording (not the terser `HELP_TEXT` footer wording, which is being removed). This constant is the single source of truth consumed by both the CLI path and the in-app path.
- **Msg/Cmd plumbing**: add `MSG_HELP` to `MsgType` (`msg.h`) and `CMD_HELP` to `CmdType` (`cmd.h`, no extra `Cmd` fields needed — no path/arguments). In `handle_nav()` (`update.c`), add `case MSG_HELP:` that sets `out_cmd->type = CMD_HELP` and nothing else, mirroring the existing `case MSG_PREVIEW:` shape. `handle_select()` (`update.c:1033`) gets no new case for `MSG_HELP` — it falls through to the existing `default: break`, so `h` is a silent no-op in `MODE_SELECT`.
- **Key binding**: in `translate_event()` (`src/dired.c`), map `ev.ch == 'h'` to `MSG_HELP` unconditionally in the same `else if` chain as the other single-letter commands (not mode-gated at the translation layer — the mode gating happens in `update.c` as described above).
- **`execute_help()`** (new function in `src/dired.c`): mirrors the existing tmp-file-then-preview shape already used by `execute_open_archive_member()` (`src/dired.c:546-568`, which extracts an archive member to a `mkdtemp`'d temp file and calls `execute_preview()` on it):
  1. Create a temp dir/file the same way `extract_member_to_tmp()` does (`mkdtemp` under `$TMPDIR` or `/tmp`).
  2. Write `HELP_CONTENT` into that file.
  3. Call the existing `execute_preview(tmp_path)` unchanged — this reuses the plain-text preview branch (`tb_shutdown(); execlp("less", "-R", tmp_path, NULL); tb_init();`) with no modification to `execute_preview()` itself.
  4. `remove()` the temp file and `rmdir()` the temp dir afterward (synchronous — `execute_preview()` blocks until `less` exits via `wait`/`waitpid`).
  5. Return whatever `Msg` `execute_preview()` returned.
- **Dispatch**: wire `CMD_HELP` into the command-execution switch in `src/dired.c` (alongside `CMD_PREVIEW`, etc.) to call `execute_help()`.
- **`print_help()` rewrite**: replace the current sequence of individual `printf()` calls for the controls list with a single print of `HELP_CONTENT`. Drop the `Usage: dired [-help]` line entirely. Keep the `Build time:` and `Detected window size:` lines, printed after `HELP_CONTENT`, unchanged.
- **Footer removal**: delete the `HELP_TEXT` macro and its `add_line(&v, STYLE_NORMAL, HELP_TEXT);` call in `src/view.c` (currently the last line appended to every `View`). Update `visible_entry_rows()` in `src/helpers.c:117` from `term_height - 3 - (has_virtual_line ? 1 : 0)` to `term_height - 2 - (has_virtual_line ? 1 : 0)`, since one fewer row is now reserved (path/title line + prompt/status line, no more footer line).
- **`Readme.md`**: left untouched — not part of this PRD's scope.

## Testing Decisions

Good tests here exercise only externally observable behavior (the `Msg`/`Cmd`/`View` produced), not internals — consistent with the existing `update_test.c`/`view_test.c`/`helpers_test.c` style, which construct a `Model`, feed it through `update()`/`view()`, and assert on the resulting `Cmd`/`View`.

- **`update.c`**: add a case to `update_test.c` asserting `MSG_HELP` in `MODE_NAV` produces `out_cmd->type == CMD_HELP` with the model otherwise unchanged, following the existing `MSG_PREVIEW` test (`update_test.c:354-365`). Add a case asserting `MSG_HELP` in `MODE_SELECT` is a no-op (model and `Cmd` both unchanged), following the pattern of other messages `handle_select()` doesn't explicitly handle.
- **`helpers.c`**: `visible_entry_rows()` already has a table-driven test (`helpers_test.c:265`) — update its expected values for the new `term_height - 2` formula rather than adding a new test.
- **`view.c`**: several existing assertions in `view_test.c` depend on the footer line's presence and must be updated, not left as new failures:
  - `view_test.c:34-35` (`line_count < 4` minimum)
  - `view_test.c:214-215` (`line_count != 4`, comment explicitly cites "path, prompt, 1 entry, help")
  - `view_test.c:515` (`entry_lines = v.line_count - 3`)
  - `view_test.c:531`/`561` (loops bounded by `v.line_count - 1` to skip the footer)
  Each needs its line-count arithmetic/comment adjusted to drop the footer row.
- **Not unit-tested, by design, consistent with existing untested precedent in this codebase**:
  - `translate_event()`'s new `h` → `MSG_HELP` mapping — `translate_event` is `static` in `dired.c` and isn't exercised by the test suite for any existing key today.
  - `execute_help()` — spawns `less` via `execute_preview()`'s `tb_shutdown()`/`execlp()`, same reason `execute_preview()` itself has no test today.
  - `print_help()` — CLI printf path, same as today.
- No new test files are needed; all changes are covered by extending the three existing pure-function test files.

## Out of Scope

- Syncing or regenerating `Readme.md`'s Controls block from `HELP_CONTENT`.
- Any in-TUI (termbox-rendered) help overlay/mode — the page is an external `less -R` pager view, not a new `AppMode`.
- Scroll/pagination logic inside dired itself for the help content — `less` already handles that.
- Making `h` do anything in `MODE_SELECT` beyond the current no-op.
- Changing the wording/content of the controls list beyond dropping the CLI's `Usage:` line.

## Further Notes

- This consolidates 3 drifting copies of the controls list down to 2 (`HELP_CONTENT` shared by CLI + in-app page, and `Readme.md`'s independent copy), rather than 1, since `Readme.md` is explicitly out of scope.
- The `execute_help()` shape is a near-exact structural match for `execute_open_archive_member()` — worth writing it right next to that function in `src/dired.c` for discoverability, since it's the same "materialize to tmp file, preview, clean up" pattern with a static buffer instead of an extracted archive member.

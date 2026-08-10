---
title: "Run shell command from dired"
description: "Getting a quick command's output today means quitting dired (or suspending it) to a real shell, then coming back — there's no way to run an ad-hoc command against the current directory without leaving the browser."
status: needs-triage
---

## Problem Statement

Today, if you want to run a one-off shell command against the directory
you're currently browsing in dired — `grep` for something across files,
`git status`, `wc -l *.c`, whatever — there's no way to do it from inside
the app. You have to quit (or background) dired, open a real shell, `cd`
back to the same place, run the command, then relaunch dired and navigate
back to where you were. This is exactly the kind of "drop out to a shell
and come back" friction that the existing preview feature already solved
for *looking at a file's content* (via `more`); it doesn't yet exist for
*running a command*.

## Solution

A new colon-prompt command mode, styled after vim's `:` command line: press
`:`, type a command starting with `!` (e.g. `!grep -rn TODO .`), press
Enter, and dired shells out to run it — via `sh -c`, so pipes, globs,
redirects, and env vars all work exactly as they would in a real shell —
with the current dired directory as its working directory. Both stdout and
stderr are merged and paged through `more`, exactly the same
`tb_shutdown()` → fork/exec → `wait()` → `tb_init()` pattern already used
for text/hex file preview. No new panel, no live-streaming output, no
change to the "no panels" principle `001-foundation.md` established: this
is the same "hand the terminal to an external program, then take it back"
model the app already uses three times over (vim, `more`, hexdump|more).

## User Stories

1. As a user, I want to press `:` to open a command prompt, so that I can run an ad-hoc shell command without leaving dired.
2. As a user, I want to type a command prefixed with `!` (e.g. `!ls -la`), so that the prompt's syntax leaves room for other command types later without ambiguity today.
3. As a user, I want my command interpreted by a real shell (`sh -c`), so that pipes, globs, redirects, and environment variables all behave the way they would in a normal terminal.
4. As a user, I want the command to run with dired's current directory as its working directory, so that relative paths in my command resolve the way I expect, exactly as if I'd `cd`'d there myself.
5. As a user, I want both the command's normal output and its error output shown together, so that I see everything the command produced (e.g. a failing build's stderr) without anything silently dropped.
6. As a user, I want the combined output paged through `more`, exactly like file preview already works, so that I don't have to learn a new set of controls (space/enter to page, `q` to quit) just because I ran a command instead of previewing a file.
7. As a user, I want dired's file listing to return, refreshed, after I quit the pager, so that any files the command created, deleted, or modified are immediately reflected without a manual reload.
8. As a user, I want pressing Escape while composing a command to cancel back to normal browsing with nothing run, so that I can back out of a command I didn't mean to type.
9. As a user, I want pressing Enter on an empty prompt, on text with no `!` prefix, or on a bare `!` with nothing after it, to behave exactly like pressing Escape (cancel, nothing run), so that I get no confusing error for input that isn't a valid command yet.
10. As a user, I want a failing command (non-zero exit status) to still show me its output in the pager rather than being treated as an app-level error, so that seeing *why* it failed (which is right there in the output) isn't blocked by dired second-guessing the command.
11. As a developer, I want the command-prompt key handled through the same `Msg`/`Cmd`/`update()` flow as every other action, so that no bespoke input-handling path is introduced just for this feature.
12. As a developer, I want the `!`-stripping and cancel-condition logic to be pure and covered by table-driven tests, so that the trickiest part of this feature (deciding what counts as a runnable command) is verified without needing to fork a real shell in a test.
13. As a developer, I want the actual command execution (fork/exec/pipe/wait) explicitly excluded from unit tests, consistent with how `execute_preview`, `execute_launch_editor`, and `run_piped` are already excluded, so that this feature doesn't introduce a new, inconsistent testing expectation.
14. As a developer, I want the existing `run_piped` helper extended (not duplicated) to merge a writer's stderr into its pipe, so that the hex-dump-preview call site and this new call site share one piping implementation instead of two near-identical ones.

## Implementation Decisions

- **New `AppMode`**: `MODE_RUN_CMD`. No new `Model` fields — reuses the existing `edit_buf`/`edit_len` text-entry fields exactly as `MODE_RENAME`/`MODE_CREATE` already do.
- **New `Msg`**: `MSG_RUN_CMD`, produced only in nav mode (same guard tier as `MSG_RENAME`/`MSG_NEW`), bound to the `:` key. `handle_nav`'s case for it is unconditional — unlike rename/delete, entering command mode doesn't depend on what's currently selected.
- **New `Cmd`**: `CMD_RUN`, carrying two things: the existing `path` field, reused to mean "working directory to run in" (set to `current_path`), and one genuinely new field, `cmd_text[PATH_MAX_LEN]`, holding the command string with its leading `!` already stripped off.
- **Prompt/label**: `add_prompt_line` in `view.c` gains a `MODE_RUN_CMD` case showing a `":"` label. `model_has_virtual_line` is extended to include `MODE_RUN_CMD`, so the typed buffer echoes live as a virtual row below the listing — matching `MODE_CREATE`'s live-echo behavior, not `MODE_RENAME`'s static-label-only behavior, since seeing what you're composing is the whole point here.
- **Text entry**: `translate_event`'s `text_entry` boolean (currently `mode == MODE_RENAME || mode == MODE_CREATE`) is extended to include `MODE_RUN_CMD`, so Esc/Enter/Backspace/printable-character routing is identical to Rename/Create — append-at-end / backspace-at-end only. **No cursor movement** is introduced by this PRD; left/right arrows remain unhandled in text-entry mode, same as today, for all three text-entry modes. **No command history/recall** either — every entry into command mode starts with an empty buffer, with no memory of previously run commands.
- **`!`-prefix parsing** (`handle_edit`'s `MSG_ACTIVATE` case for `MODE_RUN_CMD`): if `edit_buf` does not start with `!`, or the remainder after stripping exactly one leading `!` is the empty string, no `Cmd` is emitted (`out_cmd->type` stays `CMD_NONE`) and the mode resets via the existing `cancel_edit` path — functionally identical to pressing Escape. This covers all three edge cases uniformly: empty buffer, missing prefix, bare `!`. A stripped remainder that is non-empty but whitespace-only (e.g. `"! "`) is treated as a valid (if useless) command, not specially cancelled — no trimming is performed, only the exact-empty check.
- **Execution** (`execute_run_cmd(cwd, cmd_text)` in `dired.c`): `tb_shutdown()`, then the existing `run_piped` helper runs `{"/bin/sh", "-c", cmd_text, NULL}` as the writer piped into `{"more", NULL}` as the reader — the same helper already used for hexdump-into-pager. The writer child `chdir(cwd)`s immediately before `execvp`, which does not violate `001-foundation.md`'s "no `chdir()`" rule (that rule concerns the main process never relying on relative paths; a forked child that chdirs immediately before exec-ing and exiting never affects the main process's cwd).
- **`run_piped` extension**: currently only the writer's stdout is redirected into the pipe. It's extended to also `dup2` the writer's stderr onto the same pipe fd, merging both streams — a one-line change to the shared helper rather than a second near-duplicate piping function. This is harmless for the existing hexdump call site (which doesn't meaningfully write to stderr) and required for this feature (stdout+stderr must appear interleaved in the pager, like a normal terminal).
- **No shell-injection concern**: the command text is *user-typed input the user intends to run as a shell command*, unlike the filename-as-argument case `run_argv`/`run_piped`'s existing "never a shell" convention was built to protect against. `sh -c` is used deliberately here; this does not change the "never a shell" convention for any existing call site (rename/copy/move/delete/preview all keep using `execvp`/`execlp` with argument vectors, untouched by this PRD).
- **No exit-status inspection**: matches the existing convention already established for the pager and hex-dump programs (`005-hex-preview.md`). After both children are waited on, `execute_run_cmd` always returns `MSG_OP_SUCCEEDED`, `tb_init()`s, and returns. Via the existing generic `MSG_OP_SUCCEEDED` handling in `update()`, this also triggers a directory reload — useful since the command may have created, deleted, or modified files — with no special-casing needed for this feature.
- **No output size limit or truncation**: the whole command's output is piped through, same as the existing hex-dump/preview convention; the pipe's own backpressure and `more`'s own quit behavior are considered sufficient, consistent with `005-hex-preview.md`'s equivalent decision.
- **Help bar**: `HELP_TEXT` in `view.c` gains a `": Run command"` (or similar) segment.
- **No new panel, no async execution, no change to `Cmd`'s synchronous single-process model.** This feature is entirely: one new `AppMode`, one new `Msg`, one new `Cmd` variant plus one new `Cmd` field, one new `handle_nav` case, one new `handle_edit`/`MSG_ACTIVATE` case, one new `execute_*` function, a one-line extension to an existing helper, one key binding, one help-text edit.

## Testing Decisions

- Good tests here assert on `update()`'s returned `Model`/`Cmd`, never on terminal output, real filesystem state, or by shelling out to a real `sh`/`more` — consistent with the testing conventions already established across the roadmap (`001-foundation.md`, `002-file-preview.md`).
- **`handle_nav`'s `MSG_RUN_CMD` case** (via `update()`): table-driven, asserting the resulting `Model` has `mode == MODE_RUN_CMD` and empty `edit_buf`, and `out_cmd->type == CMD_NONE`. Prior art: existing `MSG_NEW`/`MSG_RENAME` coverage from foundation.
- **`handle_edit`'s `MODE_RUN_CMD`/`MSG_ACTIVATE` case** (via `update()`): table-driven, one case per `{edit_buf content} -> {expected Cmd}` — empty string, no `!` prefix (e.g. `"ls"`), bare `!`, a valid command (e.g. `"!ls -la"` → `CMD_RUN` with `cmd_text == "ls -la"`), and a prefix-plus-whitespace-only remainder (e.g. `"! "` → still `CMD_RUN`, not cancelled). This is the highest-value test target in this PRD — it's where all the edge-case decisions above live, and it's fully pure/testable without touching a filesystem or process.
- **`execute_run_cmd`** (fork/exec/pipe/wait) is explicitly **not** unit tested, matching the existing, already-established convention that excludes `execute_preview`, `execute_launch_editor`, and `run_piped` from unit tests — validated manually by running the app against a command with only stdout, a command with only stderr, a command with both, a command that fails (non-zero exit), and a command that creates/deletes a file (confirming the listing reflects it after quitting the pager).
- **The `run_piped` stderr-merging extension** is likewise not separately unit tested (it's part of the same untested fork/exec/pipe helper), but manual validation should explicitly confirm the existing hex-dump-preview call site still behaves correctly after the extension (no regression to `005-hex-preview.md`'s shipped behavior).
- No scripted terminal integration tests for the shell command or the pager — same "manual validation only" convention already established for every other fork/exec feature in this codebase.

## Out of Scope

- **Cursor movement within the command buffer** — left/right arrow key navigation mid-string. The buffer stays append-at-end/backspace-at-end only, same as Rename/Create today. A candidate for a future PRD, and if built later, whether it should also retrofit onto Rename/Create is an open question for that PRD, not this one.
- **Command history/recall** — no up/down recall of previously run commands. Every entry into command mode starts empty.
- **A live in-app output panel/window** — explicitly not built; this PRD's "window like preview" is the same external-pager pattern as file preview, not a new in-app panel, which would conflict with `001-foundation.md`'s roadmap-wide "no panels" principle.
- **Async/streaming execution** — the command still runs synchronously, blocking the main loop exactly like every other `Cmd` today (editor launch, file preview). If a command hangs, dired hangs until it's killed or exits, same as launching vim on a file today. The not-yet-built `11-async-action-queue.md` is unrelated to this PRD.
- **Exit-status-aware behavior** — no distinction between a successful and a failing command; both page their output and both trigger the same reload-on-return behavior.
- **Any other command prefix or command type besides `!`** — the prompt's generic "cmd mode" framing intentionally leaves room for future prefixes, but none are defined or implemented by this PRD. Input without a recognized prefix is treated as cancelled, not as an error.
- **Making the pager configurable** (`less`, `$PAGER`, etc.) — stays hardcoded to `more`, consistent with the existing preview features' hardcoding.
- **Output size limits, truncation, or streaming progress indicators** — none added, consistent with `005-hex-preview.md`'s equivalent decision.

## Further Notes

Depends on `001-foundation.md` (`Model`/`Msg`/`Cmd` architecture, `update`/`view` split, the "no panels" and "never a shell for untrusted filename arguments" principles) and directly reuses machinery from `002-file-preview.md` (the `tb_shutdown()`/fork-exec/`wait()`/`tb_init()` pager-launch pattern) and `005-hex-preview.md` (the `run_piped` two-process piping helper, which this PRD extends with stderr merging rather than duplicating). This is the first feature to deliberately invoke a shell (`sh -c`) rather than `execvp`/`execlp` with an argument vector — a considered exception to the existing "never a shell" convention, justified because the command text here is trusted, intentional user input (the whole point of the feature), not an untrusted filename being interpolated into a command the app constructs itself.

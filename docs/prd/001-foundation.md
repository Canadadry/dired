---
title: "Foundation: pure core (Model/Msg/Cmd) + termbox2 migration"
description: "dired's logic, rendering, and I/O are tangled into one ncurses-driven main() loop, making it impossible to test anything without a real terminal and filesystem."
status: done
---

## Problem Statement

`src/dired.c` is a single ~360-line file where keyboard handling, ncurses
rendering calls, filesystem syscalls (`rename`/`fopen`/`mkdir`/`rmdir`/`unlink`),
and global mutable state (`entries[]`, `selected`, `current_path`, `mode`,
`edit_buf`) are all interleaved inside the same functions. Several functions
(`delete_selected()`) even block on a second `getch()` call mid-function to
read a confirmation keypress. None of this can be tested without a live
terminal and a real directory on disk, and every future feature (preview,
copy/move, trash, multi-select...) would have to be wedged into this same
tangle, making each one progressively riskier to add.

Separately, the project currently depends on the system `ncurses` dev
package to build at all (`-lncurses`, `#include <ncurses.h>`), which isn't
guaranteed to be present in every environment the project is built in.

## Solution

Restructure dired around a pure core and a thin impure shell, so that all
decision-making logic is unit-testable without a terminal or filesystem, and
migrate rendering from ncurses to the already-vendored `termbox2` so the
project builds with nothing beyond a C compiler.

Concretely: an explicit `Model` struct replaces the scattered globals; a pure
`update(Msg, Model) -> {Model, Cmd}` function replaces the inline logic
currently spread across `main()`'s switch statement and helper functions
like `start_edit`/`cancel_edit`/`validate_edit`/`delete_selected`; a pure
`view(Model) -> renderable` function replaces `draw()`; and `main()` becomes
the only place that touches the terminal (via termbox2) or executes a `Cmd`
against the filesystem.

## User Stories

1. As a developer, I want the file-manager's decision logic separated from its I/O, so that I can write and run tests without spawning a terminal or touching a real directory.
2. As a developer, I want a single `Model` struct describing all application state, so that I don't have to track which global variables are relevant to a given screen or action.
3. As a developer, I want keyboard input translated into a closed set of `Msg` values before any logic runs, so that `update()` never has to interpret raw keycodes.
4. As a developer, I want `update()` to be a pure function (`Msg` + `Model` in, new `Model` + `Cmd` out), so that every state transition is a single, testable, deterministic call.
5. As a developer, I want side effects (renaming, creating, deleting, loading a directory, launching vim) expressed as a `Cmd` value returned by `update()`, not performed inline, so that `update()` never touches the filesystem directly.
6. As a developer, I want `main()` to be the only function that executes a `Cmd`, so that all filesystem/process side effects are in one auditable place.
7. As a developer, I want the result of executing a `Cmd` to feed back into `update()` as a new `Msg`, so that the full request/response cycle (e.g. "directory loaded", "rename failed") stays inside the pure core's control flow.
8. As a developer, I want the delete confirmation prompt ("Delete 'x'? [y/N]") to be an explicit `Model` state instead of a nested blocking key read, so that `update()` never blocks waiting for a second keypress mid-call.
9. As a developer, I want a failed operation's error message to be an explicit `Model` state instead of a nested blocking key read, so that error display follows the same Msg-driven flow as everything else.
10. As a developer, I want `view()` to turn a `Model` into a description of what should be on screen, without calling any termbox2 (or ncurses) function directly, so that I can assert on rendered output in tests.
11. As a developer, I want `view()`'s output to describe each line's meaning with a semantic style tag (e.g. "this is the selected row", "this is a prompt", "this is an error") rather than a raw terminal color/attribute, so that `view()` stays decoupled from whichever rendering library `main()` happens to use.
12. As a developer, I want `main()` to be the only place that translates a semantic style tag into an actual termbox2 attribute, so that a future rendering-library change touches one place, not `view()`.
13. As a user, I want the application to behave exactly as it does today (navigate, open in vim, rename, create file/directory, delete with confirmation, quit) after this refactor, so that this internal restructuring doesn't regress anything I already rely on.
14. As a developer, I want the app to build without requiring `libncurses-dev` to be installed, so that a plain C compiler is enough to build the project.
15. As a developer, I want `build.c` to compile `vendor/termbox2.h`'s implementation into one translation unit (`TB_IMPL`) instead of linking `-lncurses`, so that termbox2 is the only rendering dependency.
16. As a developer, I want termbox2's key/event constants mapped onto the existing `Msg` vocabulary (arrows, enter, escape, backspace, printable characters), so that the input-translation step is the only place aware of termbox2's specific event shape.
17. As a developer, I want table-driven tests for `update()`, `view()`, and any small pure helpers, so that adding a new behavior later is "add a row to a table" rather than "write a new test function".
18. As a developer, I want the existing `test/minitest.h` harness reused as-is (its `Case`/`TEST_GROUP`/`TEST_ERRORF` pattern already fits table-driven tests), so that no new test tooling has to be built or learned.

## Implementation Decisions

- **Model**: a single struct holding everything currently split across `entries[]`, `entry_count`, `selected`, `current_path`, `mode`, `edit_buf`, `edit_len`, `virtual_line`. `mode` becomes an explicit state enum that also covers the two new states this PRD introduces: a delete-confirmation state and an error-display state (each of these decisions previously mixed a prompt/message string plus a blocking read into one function; both become `Model` fields instead — a pending-confirmation target and an error message string).
- **Msg**: a closed enum representing every input the app reacts to — navigation keys, enter/open, escape, backspace/delete, printable character entry, validate/confirm, plus the "effect completed" messages that come back from a `Cmd` (directory loaded, operation succeeded, operation failed with an error). Raw termbox2 key events are translated into `Msg` at the `main()` boundary, not inside `update()`.
- **Cmd**: describes exactly one effect to run — load a directory, rename, create a file, create a directory, delete (permanent), or launch an external editor (fork/exec + wait). `update()` returns `{Model, Cmd}` (a "no effect" `Cmd` variant covers the common case). `main()` executes the returned `Cmd` synchronously, immediately, in the same process, and constructs the resulting `Msg` from the outcome, then calls `update()` again with it. This is deliberately synchronous and single-process for this PRD — no queue, no async, no second process. The `Cmd` type should be defined so that a later PRD can add new variants (e.g. an async/batched variant) without reshaping `Model` or `update()`'s signature.
- **view() output**: a list of lines, each with rendered text and a semantic style tag (`STYLE_NORMAL`, `STYLE_SELECTED`, `STYLE_PROMPT`, `STYLE_ERROR`, etc. — exact set driven by what `Model`'s states need to display). `view()` never references a termbox2 type or constant. `main()` owns the one lookup table mapping each semantic tag to actual termbox2 fg/bg/attribute values.
- **main() loop**: `tb_init()` → construct the initial `Model` → run the initial `LoadDirectory` `Cmd` through the same execute-`Cmd`-then-`update()` path as every later reload (no separate direct-population code path for startup) → loop: run pending `Cmd` if any → `view(model)` → paint via termbox2 → `tb_poll_event()` → translate event to `Msg` → `update(msg, model)` → repeat. `main()` is the only function calling any `tb_*` function or any filesystem/process function (`rename`, `mkdir`, `fopen`, `opendir`, `fork`/`exec`).
- **termbox2 wiring**: `build.c` drops `-lncurses` and instead compiles a translation unit defining `TB_IMPL` before including `vendor/termbox2.h`. `dired.c`'s ncurses calls (`initscr`/`getch`/`mvprintw`/`KEY_UP`/`A_REVERSE`/...) are replaced by their termbox2 equivalents (`tb_init`/`tb_poll_event`/`tb_print`/`TB_KEY_ARROW_UP`/`TB_REVERSE`/...) confined to `main()`.
- Exact keybindings for existing actions (arrows, enter, backspace, `r`/`R`, `f`, `d`, `q`, escape) are preserved as-is; only their underlying event source changes from ncurses to termbox2.
- **Launching the external editor**: termbox2 only offers a paired `tb_init()`/`tb_shutdown()` — no suspend/resume pair. The `LaunchEditor` `Cmd`'s handler in `main()` wraps the `fork`/`exec`/`wait` in `tb_shutdown()` before and `tb_init()` after, inline in the same `Cmd`-executor, so `main()` remains the only place calling any `tb_*` function. The loop's normal next `view()` + paint redraws the screen once vim returns.
- **No `chdir()`**: `Cmd`s always carry fully-qualified absolute paths, built by string-joining `Model.current_path` with an entry name (going to the parent just strips the last path segment). The process's cwd is never touched; the `LaunchEditor` `Cmd` takes an absolute path.
- **Fixed-size limits carry over unchanged**: `MAX_ENTRIES` (1024), `NAME_MAX_LEN`, and `PATH_MAX_LEN` stay plain `#define`s, `entries[MAX_ENTRIES]` stays a static array, no dynamic allocation. Directories with more than 1024 entries are silently truncated, no error surfaced.
- **Virtual line mapping (Create vs. Rename)**: Create (file/dir) maps to an appended virtual row showing `edit_buf`. Rename maps to a state where the row keeps displaying the old name while typing, with no live update of the typed text in the row itself — only a static "Rename:" label is shown.
- **No panels**: `view()`'s output stays a flat list of styled lines, not pre-shaped as "a list containing one panel." This is a roadmap-wide principle, not just a `view()`-shape detail local to foundation work — see Further Notes.
- **Delete-confirm and error-display key handling**:
  - Delete-confirm: only `y`/`Y` confirms and performs the delete; any other keypress cancels and is consumed (not just Escape/`n`).
  - Error-display: any keypress at all dismisses the error message, consumed, not otherwise processed as a normal action.
- **Semantic style tags produce zero visual change in this PRD**: in `main()`'s lookup table mapping semantic style tags to termbox2 attributes, every tag except `STYLE_SELECTED` maps to the same plain/default attribute. The tags exist for `view()`'s semantic clarity and testability, not for visual differentiation — actual distinct coloring (for `STYLE_PROMPT`, `STYLE_ERROR`, etc.) is left to the separate later PRD (`docs/prd/triage/05-colors.md`).
- **Error surfacing is widened to all operations**: rename, create-file, create-dir, delete, and load-directory all uniformly surface failures via the same error-`Model` state, `STYLE_ERROR`, and the "any key dismisses" rule above.

## Testing Decisions

- Good tests here assert on `update()`'s and `view()`'s return values given specific inputs — never on terminal output or real filesystem state, and never by inspecting `Model` internals that aren't part of the function's actual contract.
- Table-driven style throughout: for each function under test, declare a local `Case`-shaped struct (input(s) + expected output(s)), build an array of cases, loop over it with `TEST_ERRORF` on mismatch, wrap the whole loop in one `TEST_GROUP`. Prior art: `test/minitest.h` already ships a generic `Case { left, right, expected }` example and the `TEST_GROUP`/`TEST_ERRORF` macros built for exactly this loop-over-cases shape.
- Modules to test: `update()` (the largest and highest-value target — one case per `{initial Model, Msg} -> {expected Model, expected Cmd}` transition, including the new confirm-delete and error-display states), `view()` (one case per `{Model} -> {expected lines + style tags}`), and small pure helpers carried over from the current code (`is_protected_name`, `mode_to_str`).
- `main()` and anything that calls a `tb_*` function or touches the filesystem is explicitly **not** unit tested — validated manually by running the app. No scripted terminal smoke tests in this PRD.
- No separate phase of "write tests against the current ncurses code first". The current `draw()`/`main()` shapes are being replaced wholesale (different function signatures, different responsibilities), so tests against their current shape wouldn't survive the refactor. Tests are written alongside the new `Model`/`update`/`view` as they're built.

## Open Questions

- Exact `Msg`/`Cmd` variant names and the exact enumerated set of semantic style tags `view()` emits are left to implementation — only the shape of the mechanism is specified here, naming doesn't affect architecture.

## Out of Scope

- Any new user-facing feature (preview, copy/move, sort, colors, bookmarks, trash, multi-select, async operations) — this PRD only restructures existing behavior and swaps the rendering backend.
- Async/queued `Cmd` execution and any second process — deferred to the async-action-queue PRD; this PRD's `Cmd` is synchronous and single-process, but its shape must not block that later addition.
- Multi-panel `view()` output — `view()` returns a single list of styled lines in this PRD; reworking it into multiple panels/zones is explicitly scoped into the file-preview PRD instead.
- Scripted terminal integration/smoke tests (e.g. `tmux send-keys`/`expect`) — manual validation only for now.
- Windowed/paginated directory loading (shrinking `MAX_ENTRIES` and buffering large directories) — a candidate for a future PRD.
- Live-updating the row being renamed to show typed text in place of the old name — a candidate for a future PRD.

## Further Notes

This PRD is the foundation every other PRD in this roadmap depends on — `Model`, `Msg`, `Cmd`, and the semantic-style-tag convention in `view()` are the shared vocabulary the rest of the roadmap (file preview, copy/move, trash, async queue, multi-select, etc.) is written against. `docs/tea-architecture.md` and `docs/TESTING.md` (tier-2 "AppState refactor") describe the design thinking that led here; `docs/termbox2-vendoring.md` describes the vendoring decision this PRD finishes wiring in.

**Cross-PRD flag — panels.** This PRD settles on "no panels at all" as a roadmap-wide principle. The file-preview PRD (`docs/prd/triage/02-file-preview.md`) currently assumes a multi-panel layout; that assumption should be revisited against this principle before it leaves triage.

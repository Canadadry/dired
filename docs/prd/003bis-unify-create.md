---
title: "Unify create file/dir into one key"
description: "Creating a file or a directory needs two separate keys and two separate AppModes today, which blocks the sort PRD from reusing one of those keys for its directory-grouping toggle."
status: ready
---

## Problem Statement

Creating a file (`f`) and creating a directory (`d`) are two entirely
separate flows today: two keybindings, two `AppMode` values
(`MODE_CREATE_FILE`/`MODE_CREATE_DIR`), two `Msg` types
(`MSG_NEW_FILE`/`MSG_NEW_DIR`), and two prompt labels ("Create file:" /
"Create directory:"), even though the only real difference between them is
which of two nearly-identical filesystem calls eventually runs.

This split also blocks a later feature: the sort PRD (`04-sort`) wants to
bind directory-grouping to the `d` key, but `d` is currently "new directory"
and can't be reused without first freeing it up.

## Solution

Collapse file and directory creation into a single `n` ("new") key and a
single `MODE_CREATE` mode. Instead of choosing file-vs-directory up front by
which key was pressed, the user types one name, and file-vs-directory is
inferred from that name at confirmation time: a trailing `/` means
directory, anything else means file. This matches the shorthand already
familiar from shell tab-completion (`mkdir`-style trailing slash),
collapses two near-duplicate code paths into one, and frees `d`.

## User Stories

1. As a user, I want a single key to start creating something, so that I don't have to decide file-vs-directory before I've even typed a name.
2. As a user, I want typing a name ending in `/` to create a directory, so that I can use a familiar shorthand instead of a separate directory-only key.
3. As a user, I want typing a name with no trailing `/` to create a file, so that the common case (files) needs no extra keystroke.
4. As a user, I want `foo//` (multiple trailing slashes) to still just mean directory `foo`, so that a stray extra slash doesn't produce a weirdly-named entry or an error.
5. As a user, I want pressing Enter on an empty (or slash-only) name to silently cancel, exactly like it does today for empty names, so that I don't get a confusing error for a no-op.
6. As a user, I want a slash in the middle of a name (e.g. `sub/dir`) to be attempted literally rather than rejected outright, so that if `sub` already exists as a directory, the operation just works — and if it doesn't, I get the same kind of filesystem error I'd get from any other invalid path.
7. As a developer, I want the file-vs-directory decision to live in one small, pure, testable function, so that its edge cases (trailing slash, multiple trailing slashes, empty-after-stripping) are verified without touching a real filesystem or the edit-mode state machine.
8. As a developer, I want `MODE_CREATE_FILE`/`MODE_CREATE_DIR` and `MSG_NEW_FILE`/`MSG_NEW_DIR` fully removed rather than kept alongside the new single-key flow, so the codebase doesn't carry two ways of doing the same thing.

## Implementation Decisions

- **Single mode, single message:** `MODE_CREATE_FILE` and `MODE_CREATE_DIR` are replaced by one `MODE_CREATE`. `MSG_NEW_FILE` and `MSG_NEW_DIR` are replaced by one `MSG_NEW`, bound to the `n` key. The old modes/messages are deleted outright, not deprecated or kept as aliases.
- **Decision timing:** file-vs-directory is decided only when the edit is confirmed (on `MSG_ACTIVATE`), never live while typing. The prompt label shown while in `MODE_CREATE` is a single fixed string (e.g. "Create:") for the whole duration of typing — it does not flip between "file" and "directory" wording as a trailing slash is typed or erased.
- **Classification helper:** a new pure function, in the same module and style as `find_available_name` (no I/O, deterministic, single well-defined contract), takes the raw typed buffer and reports back whether it names a file or a directory, plus the name with all trailing slashes stripped. This is the single place that owns "what does this typed string mean" — `update.c`'s `MSG_ACTIVATE` handling calls it once and dispatches to `CMD_CREATE_FILE` or `CMD_CREATE_DIR` accordingly. `CMD_CREATE_FILE`/`CMD_CREATE_DIR` and their execution (the actual `fopen`/`mkdir` calls) are unchanged — only what decides which one fires is new.
- **Trailing slash handling:** any number of trailing slashes (`foo/`, `foo//`, `foo///`, ...) are all equivalent to a single trailing slash and all strip down to directory name `foo`.
- **Empty-after-stripping:** a buffer that is empty, or contains only slashes, is treated exactly like today's empty-name case on Activate — cancel silently, no filesystem call, no error message.
- **Embedded (non-trailing) slash:** a name like `sub/dir` is not specially detected or rejected. It is classified as a file (no trailing slash) and passed through untouched to the existing path-joining and `CMD_CREATE_FILE`/`CMD_CREATE_DIR` execution, which already handles arbitrary relative paths — it will succeed or fail based on whatever the filesystem does with that literal path (e.g. fail with a "no such file or directory" style message if `sub` doesn't already exist), the same way any other filesystem error surfaces today. No new validation is added for this case.
- **Keybinding fallout:** `f` becomes unbound after this PRD, with no reuse planned here. `d` becomes unbound and is claimed by `04-sort` for its directory-grouping toggle — `04-sort` depends on this PRD shipping first. The help text is updated to replace the separate "New file"/"New dir" entries with a single "New" entry bound to `n`.

## Testing Decisions

- The classification helper is a pure function with no I/O — a table-driven unit test in the same style as `test_is_protected_name`/`test_find_available_name`, covering: a plain name (file), a name with one trailing slash (directory, slash stripped), a name with multiple trailing slashes (directory, all stripped), a slash-only or empty buffer (reported as the empty/cancel case), and a name with an embedded but non-trailing slash (classified as file, name passed through unchanged).
- The `MSG_ACTIVATE` dispatch in `MODE_CREATE` (choosing `CMD_CREATE_FILE` vs `CMD_CREATE_DIR` and building the resulting `Cmd`) is a table-driven logic test in the same style and harness as the existing `update()` tests (e.g. `test_move_selection`, the rename/create tests already in `test/update_test.c`) — construct a `Model` in `MODE_CREATE` with a given `edit_buf`, feed `MSG_ACTIVATE`, assert on the resulting `Cmd` type and path.
- `execute_create_file`/`execute_create_dir` themselves are unchanged and already covered by existing tests — no new I/O-level tests are needed for them.

## Out of Scope

- Nested directory creation (`mkdir -p` semantics). An embedded slash is passed through literally to the existing single-level `mkdir`/`fopen` calls, which are not changed by this PRD — creating `sub/dir` still requires `sub` to already exist.
- Any change to `CMD_CREATE_FILE`/`CMD_CREATE_DIR`'s execution semantics, error handling, or the underlying `mkdir`/`fopen` calls.
- Reassigning the freed `f` key to anything.
- Live-updating the prompt label or mode while typing (decision is Enter-time only, per above).

## Further Notes

Depends on `01-foundation` (uses the `Model`/`Msg`/`Cmd`/`update()` architecture it established). `04-sort` depends on this PRD landing first, since it reuses the `d` key this PRD frees up.

This PRD was scoped out of a grilling session for `04-sort`: the user wanted `d` for directory-grouping, which required freeing it from "new directory" first. It's sequenced immediately before `04-sort` for that reason, not because it was independently prioritized against the rest of the triage backlog.

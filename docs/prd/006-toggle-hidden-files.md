---
title: "Toggle hidden files"
description: "Dotfiles are always shown with no way to filter them out, cluttering listings in directories full of config/dotfiles."
status: done
---

## Problem Statement

`load_directory()` currently loads every entry in a directory except `.`
and `..` — dotfiles like `.git`, `.bashrc`, or `.env` are always shown,
with no way to hide them. In directories with many dotfiles this clutters
the listing and makes it harder to spot the files the user actually cares
about.

## Solution

Add a key that toggles whether dotfiles are shown. Hidden files are
hidden by default (matching the common `ls`/file-manager convention);
pressing the toggle key reveals them, and pressing it again hides them.
The state is session-scoped like sort/group: it survives moving between
directories but always resets to hidden on a fresh launch.

## User Stories

1. As a user, I want dotfiles hidden by default, so that a typical directory listing isn't cluttered with config files I rarely need to touch.
2. As a user, I want a single key to reveal hidden files, so that I can get to `.env`/`.git`/etc. when I actually need them.
3. As a user, I want to press the same key again to hide them, so that toggling is a single, memorable action rather than two separate bindings.
4. As a user, I want the key to work as both `a` and `A`, so that caps lock or an accidental shift doesn't stop the toggle from working.
5. As a user, I want `.` and `..` to never appear as list entries regardless of the toggle state, so that the toggle only ever affects real dotfiles, consistent with how `.`/`..` are already excluded today.
6. As a user, I want my show/hide choice to carry over as I navigate between directories in the same session, so that I don't have to re-toggle in every folder I visit.
7. As a user, I want the toggle to reset to hidden the next time I launch the app, so that a stale non-default state from a previous session never surprises me — nothing is silently persisted to disk.
8. As a user, if the file my cursor was on disappears because I just hid dotfiles, I want the cursor to land on a predictable entry (the first one in the list) rather than somewhere that feels arbitrary.
9. As a user, if I create a new file or directory whose name starts with `.` while hidden files are currently hidden, I accept that it will immediately disappear from the listing after creation — I can reveal it with the toggle if I need to see it right away.
10. As a developer, I want the hidden/shown state to be `Model` state, so that `update()` remains the single place that decides what's visible.
11. As a developer, I want the dotfile check itself to be a small pure predicate, so that it can be unit-tested the same way `is_protected_name` already is, without touching a real filesystem.
12. As a developer, I want the filtering to happen in `load_directory()`'s existing readdir loop rather than as a post-load filter, so that a directory with more dotfiles than fit in `MAX_ENTRIES` doesn't waste slots on entries that are about to be filtered out anyway, and so `Model.entries` always holds exactly the currently-visible set (consistent with how `selected` is already used as a direct index into `entries` by rename/delete/yank/activate).

## Implementation Decisions

- **Default:** a freshly-started process, and every freshly-loaded directory, begins with hidden files hidden. `Model.show_hidden` defaults to `0`/false, the same zero-value convention already used by `sort_mode`/`group_mode`.
- **Key binding:** both `a` and `A` map to a new `MSG_TOGGLE_HIDDEN` message, handled in nav mode alongside the other single-key commands (`s`, `d`, `c`, `m`, `p`, etc.) in `update.c`'s nav handler.
- **State shape and scope:** `show_hidden` is a new field on `Model`, session-scoped exactly like `sort_mode`/`group_mode` — it survives directory navigation (not reset by `MSG_DIR_LOADED`) and is never written to disk or any config file. A new process always starts hidden.
- **Filtering point:** unlike sort/group (which only reorder an already-loaded set), hiding/showing dotfiles changes *which* entries are loaded at all. Because `Model.entries` is indexed directly and 1:1 by both `view()` and every nav/index-based operation (`MSG_MOVE_UP`/`DOWN`, `ACTIVATE`, `DELETE`, rename, yank), filtering happens at load time inside `load_directory()`'s existing readdir loop — the same loop that already skips `.`/`..` via `is_protected_name` — rather than as a separate render-time filter over a fully-loaded array.
- **`load_directory()` signature:** gains a `show_hidden` parameter. When false, any entry whose name matches the new `is_hidden_name()` predicate is skipped before the `lstat`/store step, same as `.`/`..` are today.
- **New predicate `is_hidden_name()`:** a small pure function in `helpers.c`/`helpers.h`, alongside `is_protected_name()`. Returns true for any name starting with `.` — this predicate does not need to special-case `.`/`..` itself, since those are filtered separately by `is_protected_name()` in the same loop regardless of `show_hidden`.
- **Toggle triggers a reload:** `MSG_TOGGLE_HIDDEN`'s handler flips `Model.show_hidden` and emits a `CMD_LOAD_DIR` for the current directory — the same reload round-trip `update()` already performs after `MSG_OP_SUCCEEDED`. This keeps `Model.entries`/`entry_count` always in sync with the active filter, with no dual-list or per-entry "is hidden" flag needed.
- **`Cmd` carries the filter:** `Cmd` gains a `show_hidden` field so every `CMD_LOAD_DIR` site (`MSG_GO_PARENT`, `ACTIVATE` into a directory, the post-mutation reload after `MSG_OP_SUCCEEDED`, and the new toggle) passes the model's current `show_hidden` through to `execute_cmd()`, which forwards it to `load_directory()`. The very first `CMD_LOAD_DIR` issued at startup in `main()` also sets it from the model's initial (default/hidden) value.
- **Cursor fallback, changed globally:** `resort_and_relocate()`'s existing behavior — when the previously-selected name can't be found in the new entry list, clamp `selected` to `entry_count - 1` (the last entry) — changes to select index `0` (the first entry) instead. This is not scoped narrowly to the hidden-toggle case: it's the single shared fallback used by `MSG_CYCLE_SORT`, `MSG_CYCLE_GROUP`, and every `MSG_DIR_LOADED` (including reloads after delete, create, rename, copy, move, and the new toggle), so the same rule now applies uniformly everywhere that fallback fires.
- **Creating a dotfile while hidden files are hidden:** no special-casing. The existing create flow already reloads via `MSG_OP_SUCCEEDED` → `CMD_LOAD_DIR`; with `show_hidden` still false, the new dotfile is filtered right back out, and the cursor fallback above (select first) applies exactly as it would for any other "previously-selected name not found" case.
- **Help text:** `HELP_TEXT` in `view.c` gains an entry for the new key (e.g. `a: Toggle hidden`).

## Testing Decisions

- Only external behavior is tested — inputs to a pure function or a message/model pair, and the resulting output/model — never internal call sequences.
- **`is_hidden_name()`:** a table-driven unit test in `helpers_test.c`, same style as `test_is_protected_name`, covering a leading-dot name, a non-dotted name, an empty string, and (for documentation of the boundary) that `.`/`..` themselves would also match this predicate even though they're filtered separately by `is_protected_name`.
- **`load_directory(path, show_hidden)`:** extend the existing temp-directory integration test in `loaddir_test.c` (same pattern as `test_load_directory_excludes_dot_and_dotdot`) with a directory containing both a dotfile and a regular file — assert the dotfile is absent when `show_hidden` is false and present when true, while the regular file is present in both cases.
- **`MSG_TOGGLE_HIDDEN` handling:** a table-driven `update()` test in `update_test.c`, same harness as the existing `MSG_CYCLE_SORT`/`MSG_CYCLE_GROUP` tests — asserts `show_hidden` flips on the resulting model and that the returned `Cmd` is `CMD_LOAD_DIR` for the current path carrying the new `show_hidden` value.
- **Cursor fallback change:** extend/adjust the existing sort/group/`dir_loaded` tests in `update_test.c` that exercise the "previously-selected name not found" path to assert `selected` lands on `0` rather than `entry_count - 1`.

## Out of Scope

- Persisting `show_hidden` across process restarts — no config/persistence layer exists in this codebase, and none is introduced here.
- Any definition of "hidden" beyond the leading-dot convention (e.g. filesystem hidden attributes, `.hidden` marker files).
- Changing how `.`/`..` are excluded — they remain always-excluded regardless of `show_hidden`, unchanged from today's `is_protected_name` behavior.
- Any UI indicator beyond the help-text line (e.g. a persistent on-screen "hidden files: on/off" status).

## Further Notes

Depends on `01-foundation` (uses the `Model`/`Msg`/`Cmd`/`update()` architecture) and follows the same session-scoped-state and pure-comparator/predicate conventions established by `004-sort`. The cursor-fallback change (clamp-to-last → select-first) is a small behavior change to existing sort/group/reload paths, not just new code for this feature — flagged explicitly here since it wasn't obviously implied by the feature name alone.

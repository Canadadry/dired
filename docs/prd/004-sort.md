---
title: "Sort entries"
description: "Entries are listed in whatever order readdir() returns them, with no way to sort by name, size, date, or type."
status: ready
---

## Problem Statement

Entries currently appear in raw `readdir()` order, which is
filesystem-dependent and not useful for finding a specific file quickly.

## Solution

Let the user cycle through name/date/size/extension sort keys (each with
ascending and descending direction) with a single key, and independently
cycle through directory-grouping behavior with a second key. Sort state is
session-scoped: it survives moving between directories but always starts
fresh (name ascending, directories grouped first) when the app is launched.

## User Stories

1. As a user, I want to sort entries by name, so that I can find a file alphabetically.
2. As a user, I want to sort entries by size, so that I can find the largest/smallest files.
3. As a user, I want to sort entries by modification date, so that I can find recently changed files.
4. As a user, I want to sort entries by extension, so that I can group similar file types together.
5. As a user, I want one key to cycle through every sort key and direction, so that I don't need to memorize separate bindings for "change key" and "toggle direction."
6. As a user, I want the sort cycle to have a predictable, fixed order, so that repeated presses behave the same way every time.
7. As a user, I want a sensible default sort (name ascending) when I start the app, so that the very first listing I see is already useful.
8. As a user, I want my chosen sort to carry over as I navigate between directories in the same session, so that I don't have to re-apply it in every folder I visit.
9. As a user, I want the sort to reset to the default the next time I launch the app, so that a stale non-default sort from a previous session never surprises me — nothing is silently persisted to disk.
10. As a user, I want a separate key to control whether directories are grouped before files, after files, or not grouped at all, so that grouping is independent of what I'm sorting by.
11. As a user, I want directories grouped first by default, so that the common "navigate into a subfolder" case doesn't require any extra keypresses.
12. As a user, I want the cursor to stay on the same file when I change the sort or grouping, so that re-sorting never makes me lose my place.
13. As a user, I want `.` and `..` to never appear as list entries, so that a sort doesn't produce a confusing position for something I can't even interact with (going up already has its own dedicated key).
14. As a user, I want extension sorting to handle files and directories with no extension in a predictable way (grouped together, ordered by name) rather than crashing or sorting arbitrarily.
15. As a developer, I want the sort key and grouping mode to be Model state, so that `update()` remains the single place that decides ordering.
16. As a developer, I want the actual ordering comparison to be a pure, independently-tested function, so that every key/direction/grouping combination can be verified without a real directory or terminal.
17. As a developer, I want `Model.entries` re-sorted in place rather than sorted only at render time, so that `selected`'s existing use as a direct index into `entries` (shared by rename/delete/yank/activate) stays valid without those call sites needing to know anything about sorting.

## Implementation Decisions

- **Sort keys:** name, date (modification time), size, and extension — all four in v1, each available in ascending and descending direction.
- **Cycling `s`:** a single key cycles through 8 fixed states, wrapping, in this exact order: name↑ → name↓ → date↑ → date↓ → size↑ → size↓ → ext↑ → ext↓ → (back to name↑). There is no separate direction-toggle key; direction is baked into the cycle itself.
- **Default sort:** a freshly-started process begins at name↑.
- **Cycling `d`:** a second, independent key cycles a 3-state directory-grouping mode, wrapping: dirs-first → dirs-last → mixed → (back to dirs-first). `mixed` means directories interleave with files purely by the active sort key, with no special grouping. This key is freed up by `003bis`, which this PRD depends on.
- **Default grouping:** dirs-first on a freshly-started process.
- **State shape and scope:** sort key/direction and grouping mode are two independent fields on `Model`. Both survive directory navigation (`MSG_DIR_LOADED` does not reset them), the same way `yank_path` already survives navigation messages today. Neither is ever written to disk or any config file — this codebase has no persistence layer, and none is introduced for this PRD. A new process always starts at the defaults above.
- **`.`/`..` filtering:** `.` and `..` are excluded from `Model.entries` entirely, filtered out at directory-load time rather than being present-but-inert as they are today. They were already unreachable via activate/rename/delete/yank (all guarded by `is_protected_name`), and "go to parent" is already handled independently by a dedicated key, not by activating a `..` row — so removing them from the list loses no functionality and removes the need to special-case them in the sort comparator. `is_protected_name` itself is unchanged and stays useful for other protected-name checks, even though it becomes unreachable for `.`/`..` specifically once they're filtered at the source.
- **Extension-key tiebreak:** an entry with no extension (a directory, or an extensionless file — relevant whenever grouping is `mixed`) sorts as if its extension were the empty string, which naturally places it before any entry with a real extension when ascending. Entries that tie on extension (including multiple no-extension entries) are tiebroken by name, ascending. Worked example (mixed grouping, ext↑): given `zdir/` (directory, no extension), `readme` (file, no extension), `a.md`, `b.txt`, the resulting order is `readme, zdir, a.md, b.txt` — both no-extension entries cluster first, ordered by name between themselves, ahead of any entry with a real extension.
- **General tiebreak:** any key that ties (e.g. two files sharing a modification time or size) is broken by name ascending, for deterministic ordering — the same convention used for the extension key's tiebreak above.
- **Comparator as a pure module:** ordering is computed by a single pure comparison function (same style as `find_available_name`/`is_protected_name` — no I/O), taking two entries plus the current sort key, direction, and grouping mode, and returning their relative order. This is the one place that encodes every rule above.
- **In-place resort, not render-time sort:** `Model.entries` is re-sorted in place by calling the comparator, not merely reordered for display. This is necessary because `selected` is already used elsewhere as a direct index into `entries` (rename, delete, yank, and activate all index `entries[selected]` today) — if sorting only happened at render time, `selected` and the displayed order would diverge. Re-sorting happens (a) immediately after `MSG_DIR_LOADED` is handled, and (b) after every `s`/`d` keypress changes sort key, direction, or grouping mode.
- **Selection follows the file:** whenever entries are re-sorted, the model re-locates the entry that was selected before the resort (by name) in the new order and updates `selected` to its new index, so the cursor visually stays on the same file rather than staying at the same row number. If that entry is ever not found post-resort (not expected in normal operation, since resorting never adds or removes entries), fall back to the same clamping `handle_dir_loaded` already does when `entry_count` shrinks.
- **New messages:** `MSG_CYCLE_SORT` (bound to `s`) and `MSG_CYCLE_GROUP` (bound to `d`), handled alongside the other navigation messages in `update.c`'s nav-mode handling.
- **Help text:** gains entries for `s` and `d`.

## Testing Decisions

- The comparator is a pure function with no I/O — a table-driven unit test in the same style as the existing `helpers.c` tests, with cases for each sort key in each direction, each grouping mode, the extension no-extension/directory tiebreak worked example above, and a general tiebreak case (two entries sharing a sort-key value, ordered by name).
- The `s`/`d` cycling logic (state transitions through the 8 sort states and 3 grouping states, including wraparound) and the `.`/`..` filtering are table-driven logic tests in the same style and harness as the existing `update()` tests (e.g. `test_move_selection`, `test_dir_loaded`) in `test/update_test.c`.
- The "selection follows the file across a resort" behavior is tested the same way: construct a `Model` with a known selected entry, feed the message that triggers a resort, assert the new `selected` index points at the same file's new position.
- Directory loading's exclusion of `.`/`..` is covered by the existing tier-3 integration-test pattern already used for `load_directory` (temp directory, real filesystem, assert on the resulting entry list) — asserting `.`/`..` are absent from the loaded entries regardless of what's actually in the directory.

## Out of Scope

- Persisting sort or grouping state to disk/config across process restarts — explicitly decided against; no config file exists in this codebase and none is introduced here.
- Any sort key beyond name/date/size/extension (e.g. permissions, owner).
- A secondary/multi-key sort UI beyond the fixed tiebreak-by-name convention already specified for ties.
- Any change to how `.`/`..`-adjacent navigation itself works (`MSG_GO_PARENT`/left-arrow) — only their presence as list *entries* is removed.

## Further Notes

Depends on `01-foundation` (uses the `Model`/`Msg`/`Cmd`/`update()` architecture) and on `003bis` (frees the `d` key this PRD binds to directory-grouping). This was the least-specified PRD in the original roadmap — only the feature name ("tri") had been picked before this grilling session settled the rest.

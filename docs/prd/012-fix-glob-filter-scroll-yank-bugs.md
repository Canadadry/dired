---
title: "Fix four cross-feature bugs: glob overflow, scroll div-by-zero, paste collision, stale yank"
description: "A code audit found one stack buffer overflow, one divide-by-zero crash, and two silent-correctness bugs, all caused by one feature's assumptions breaking once a second feature is layered on top."
status: done
---

## Problem Statement

A manual audit of the whole codebase (not a user bug report) turned up four
defects, none of which are visible by reading any single feature's PRD in
isolation — each is a place where two already-shipped features interact
badly:

1. `011-glob-mode`'s recursive candidate list can hold up to 4096 entries,
   but `apply_filter()` writes its matches into `Model.entries`, which is
   only sized for 1024 (`MAX_ENTRIES`, the cap `001-foundation`/`load_directory`
   use for a single directory's listing). A glob/regex-glob pattern that
   matches more than 1024 files anywhere under the current tree — plausible
   on any real project — overflows `entries` and corrupts the rest of the
   `Model` struct in place, since `Model` is copied and passed by value
   everywhere. This is a genuine stack buffer overflow, not a cosmetic bug.
2. `007-list-pagination`'s scroll math (`page_snap_offset()`) divides the
   selected index by the number of visible rows with no guard. Resizing the
   terminal down to 3 rows (or 4, with a Create/Rename virtual line active)
   makes the visible-row count zero, and the very next scroll recompute
   divides by zero.
3. `003-copy-move`'s paste collision check (`find_available_name()`) is run
   against `Model.entries` — the *currently displayed* list — instead of the
   real directory contents. When `010-filter-mode` or `011-glob-mode` is
   active, `entries` is a narrowed/reshaped subset, so pasting a file whose
   name collides with something the filter/glob is currently hiding silently
   overwrites it instead of getting auto-renamed the way `003-copy-move`
   intends.
4. A pending yank (`yank_path`) isn't invalidated when the file it points at
   is renamed, trashed, or permanently deleted. A later paste then acts on a
   stale path — best case it fails outright, worst case a new, unrelated
   file created under the same name gets silently copied/moved instead of
   what the user actually yanked.

## Solution

Fix all four independently; none of them require UI/keybinding changes or
touch `view()`'s rendering logic:

1. `apply_filter()` gains a destination-capacity bound and truncates its
   output at that bound instead of trusting the caller — mirroring the
   truncation/"results incomplete" pattern `011-glob-mode` already
   established for the raw recursive-walk cap, just applied one stage later
   (at match time instead of walk time).
2. `page_snap_offset()` treats a non-positive visible-row count as "nothing
   fits, stay at the top" instead of dividing by it.
3. `MSG_PASTE`'s collision check runs against `unfiltered_entries` (the
   real, currently-loaded directory contents) instead of `entries`.
4. `MSG_RENAME`'s commit and both delete confirmations (`x` permanent,
   Backspace trash) clear `yank_path` when the path they're about to act on
   is the same one a pending yank points at — matching the optimistic-clear
   convention `MSG_PASTE` already uses (`yank_path` is cleared when the
   `Cmd` is issued, not when its result comes back).

## User Stories

1. As a user, I want to glob/regex-glob a directory tree with more than 1024 matching files without the app corrupting its own state, so that a big or common pattern doesn't crash or misbehave the app.
2. As a user, I want a glob search that matches too many files to tell me its results are incomplete, the same way an over-large candidate list already does, so that "too many matches" is always a visible, dismissible notice rather than silent corruption or a silent partial view.
3. As a user, I want to resize my terminal to a very small size without the app crashing, so that an accidental or deliberate tiny window doesn't kill my session.
4. As a user, when I've narrowed the listing with a filter or glob and then paste a file whose name collides with something outside the narrowed view, I want it auto-renamed (not silently overwritten), so that pasting while filtered/globbed is exactly as safe as pasting in the full listing.
5. As a user, if I yank a file and then rename it before pasting, I want the pending yank to track the fact that its source changed, so that I don't get a confusing failed paste or, worse, an unrelated file getting copied under the old name.
6. As a user, if I yank a file and then trash or permanently delete it before pasting, I want the pending yank cleared, so that a stale "Yanked: X" status line never lingers over a source that no longer exists.
7. As a developer, I want `apply_filter()`'s new capacity bound to be exercised by the same table-driven test convention already used for its filter-matching behavior, so that this fix has the same test coverage rigor as the feature it's patching.
8. As a developer, I want `page_snap_offset()`'s new non-positive guard covered by the same table-driven test convention already used for its normal-case behavior.
9. As a developer, I want the paste-collision fix and the yank-invalidation fix both covered at the `update()` level, consistent with how `003-copy-move`'s existing yank/paste tests are structured.

## Implementation Decisions

- **`apply_filter()` interface change** (`helpers.c`/`helpers.h`): gains an output-buffer capacity parameter and an `int *out_truncated` output parameter. The internal match loop stops appending once `out_entry_count` reaches capacity and sets `*out_truncated = 1` for the rest of the source instead of continuing to write past it. All three existing call sites (`handle_dir_loaded`, `recompute_filter_live`, `recompute_glob_live`, `handle_glob_built`'s non-live branch) pass `MAX_ENTRIES` as the capacity. For the flat-filter and directory-load call sites this is a no-op in practice (source is already capped at `MAX_ENTRIES` by `load_directory()`); it only ever engages for the glob call sites, where the candidate pool can be up to `GLOB_MAX_CANDIDATES` (4096).
- **Glob-side truncation surfacing** (`update.c`): when `apply_filter()` reports `out_truncated` from a glob-sourced call (`recompute_glob_live` or `handle_glob_built`'s non-live branch), route into the same `MODE_ERROR` + `error_reenter_glob` mechanism `011-glob-mode` already uses for the raw-walk cap, with a distinct message (e.g. "too many matches (1024+) - narrow your pattern") so the user can tell "the recursive walk itself was capped" (existing message, mentions `GLOB_MAX_CANDIDATES`) apart from "your pattern matched too much of what was found" (new message, mentions `MAX_ENTRIES`). No new `Model` field is needed — this reuses `glob_truncated`/`error_reenter_glob` exactly as before, just fed from a second source.
- **`page_snap_offset()` fix** (`helpers.c`): guard `visible_rows <= 0` and return `0` immediately (top of the list, no scroll) rather than computing `selected / visible_rows`. The fix lives in `page_snap_offset()` itself, not in `visible_entry_rows()` — `visible_entry_rows()` returning a non-positive number is meaningful information other call sites already branch on (`MSG_CYCLE_PAGE`'s existing `visible_rows > 0` guard, `view()`'s `end` computation), so clamping it at the source would hide that signal from them.
- **Paste collision fix** (`update.c`, `handle_nav`'s `MSG_PASTE` case): `find_available_name()` is called with `out_model->unfiltered_entries`/`out_model->unfiltered_count` instead of `out_model->entries`/`out_model->entry_count`. `unfiltered_entries` is always the real, currently-loaded contents of `current_path` (populated on every `MSG_DIR_LOADED`, independent of filter/glob state) — exactly what "does the paste destination already contain a colliding name" needs to check, regardless of whether a filter or glob is currently narrowing the view.
- **Yank invalidation on rename** (`update.c`, `handle_edit`'s `MODE_RENAME` branch of `MSG_ACTIVATE`): after building the rename source path (`out_cmd->path`), if it equals `out_model->yank_path`, clear `yank_path` before returning — same optimistic-clear timing `MSG_PASTE` already uses (cleared when the `Cmd` is issued, not gated on the eventual `MSG_OP_SUCCEEDED`/`MSG_OP_FAILED`).
- **Yank invalidation on delete** (`update.c`, `handle_confirm_delete()`): after building the delete/trash target path (`out_cmd->path`), if it equals `out_model->yank_path`, clear `yank_path` before returning. Covers both `x` (permanent delete) and Backspace (trash), since both paths go through this same handler.
- **No new `Msg`/`Cmd`/`AppMode` variants** — all four fixes are internal to existing handlers and pure helpers; nothing about the key bindings, modes, or `Cmd` surface changes.

## Testing Decisions

Good tests here assert on pure function outputs and on `update()`'s
returned `Model`/`Cmd`, never on terminal rendering or real filesystem
state — the same convention already established across every prior PRD's
Testing Decisions section (`helpers_test.c` for pure logic, `update_test.c`
for the `Model`/`Cmd` transitions, no scripted rendering tests).

- **`apply_filter`** (`helpers_test.c`): extend its existing table-driven coverage with cases where the match count exceeds the supplied capacity — asserting the output count is capped exactly at capacity and `out_truncated` is set — and a regression case confirming `out_truncated` stays `0` when matches fit within capacity (covers the existing flat-filter/directory-load behavior, which must be unaffected).
- **`page_snap_offset`** (`helpers_test.c`): extend its existing table-driven coverage with `visible_rows == 0` and `visible_rows < 0` cases, asserting a `0` result rather than a crash.
- **Glob-sourced truncation via `update()`** (`update_test.c`): a new case alongside the existing `test_glob_built_truncated_enters_error_with_candidates_populated`/`test_glob_truncation_error_dismiss_reenters_glob_composing` pair, feeding `MSG_GLOB_BUILT` a candidate pool larger than `MAX_ENTRIES` whose pattern matches all of it, asserting `entry_count` caps at `MAX_ENTRIES` and the mode transitions into `MODE_ERROR`/`error_reenter_glob` with the new message — mirroring the existing raw-walk-truncation test's structure exactly.
- **Paste collision fix** (`update_test.c`): the existing `test_paste_pending` table's "paste with name collision gets a numbered duplicate" case currently populates only `entries` (via `make_nav_model`, which never sets `unfiltered_entries`) — this case must be updated to populate `unfiltered_entries`/`unfiltered_count` instead, since that's what the fixed code path reads. Add a new case that populates a *non-matching* `entries` (simulating an active filter/glob) alongside a colliding `unfiltered_entries`, asserting the collision is still caught and the destination gets a numbered duplicate rather than the bare colliding name.
- **Yank invalidation on rename/delete** (`update_test.c`): new cases alongside the existing yank-related tests (the nav-mode `MSG_CANCEL`-clears-yank tests and `test_paste_pending`), covering: renaming the entry a pending yank points at clears `yank_path`; renaming a *different* entry while a yank is pending leaves `yank_path` untouched; confirming delete/trash on the entry a pending yank points at clears `yank_path`; confirming delete/trash on a different entry leaves it untouched.
- No `view_test.c`/rendering changes needed — none of these four fixes alter what `view()` does with a given `Model`; they only make sure `Model` never reaches an invalid state (overflowed `entry_count`, corrupted fields, a stale `yank_path`) in the first place.

## Out of Scope

- **Growing `MAX_ENTRIES` or making `entries` dynamically sized** — considered and explicitly rejected in favor of capping+truncating, to avoid growing `Model`/`View`'s per-copy cost (both are passed/copied by value throughout the codebase) and to stay consistent with the truncation UX `011-glob-mode` already established.
- **Any change to what counts as a "collision" in `find_available_name()`** — unchanged; only which entry list it's checked against changes.
- **A general "undo the last yank source change" or yank history** — the fix is narrowly "don't act on a path that's known to be gone/renamed," not a broader yank lifecycle redesign.
- **Auditing for further, not-yet-found instances of the same two bug classes** (fixed-capacity mismatches; stale cached paths) elsewhere in the codebase — this PRD covers exactly the four defects identified in the audit that produced it, not a general sweep.

## Further Notes

Depends on `001-foundation` (`Model`/`Msg`/`Cmd`/`update()` architecture),
`003-copy-move` (yank/paste, `find_available_name`), `007-list-pagination`
(`page_snap_offset`/`visible_entry_rows`), `010-filter-mode` (`apply_filter`,
`unfiltered_entries`), and `011-glob-mode` (`glob_candidates`,
`GLOB_MAX_CANDIDATES`, the truncation/`error_reenter_glob` pattern this PRD
extends rather than duplicates). All four bugs were found by static
inspection only (no reproduction run) during a bug-hunt requested directly
in conversation, then triaged into this single PRD once the fix approach
for the most architecturally significant one (the glob overflow) was
confirmed with the user: cap-and-truncate, not grow-the-buffer or
dynamic-allocate.

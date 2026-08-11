---
title: "Filter listing by filename (plain / regex)"
description: "Finding a file in a large directory means scanning the whole listing by eye or paginating through it, with no way to narrow the view to only the entries you care about."
status: done
---

## Problem Statement

Today, the only way to locate a file in a directory dired is browsing is to
scroll/paginate through the full listing and read every name. In a
directory with dozens or hundreds of entries, finding "the file with `report`
in its name" or "anything matching `*.log`" means scanning by eye, with no
way to narrow what's on screen.

## Solution

Two new nav-mode keys open a live, filename-only filter over the current
directory's listing:

- `f` — filter by **plain substring**: type `report`, and only entries
  whose name contains `report` stay visible.
- `F` — filter by **extended POSIX regex** (`REG_EXTENDED`, via the
  standard library's `<regex.h>` — no vendored dependency): type
  `\.(c|h)$`, and only entries whose name matches stay visible.

Both filter the already-loaded directory listing in memory — no extra disk
I/O beyond the directory load that already happens. The list narrows live,
on every keystroke, the same way `fzf` or similar interactive filters work;
this is a new interaction pattern for this codebase (every existing
text-entry mode — Rename/Create/`:` Run Command — only takes effect on
Enter). Filtering applies uniformly to files and directories alike: a
non-matching directory is hidden exactly like a non-matching file, with no
special-casing to keep directories visible so you can navigate "through"
them.

Pressing Enter commits the current pattern and returns to normal
navigation with the filtered view intact and persistent — up/down, rename,
delete, yank, etc. all operate on the narrowed list until the filter is
explicitly cleared. Pressing Esc while composing always fully clears the
filter (never reverts to whatever was active before you started editing).
The filter is cleared automatically whenever you navigate to a different
directory (parent or into a subdirectory), but survives same-directory
reloads (e.g. after a paste, rename, or delete refreshes the listing).

This also closes an unrelated small gap surfaced while designing the
filter/yank status-line interaction: Esc in nav mode is currently unbound
(a no-op). This PRD binds it to cancel a pending yank — today, once you
yank a file, the only way to clear that pending state is to paste it
somewhere; there's no way to back out.

## User Stories

1. As a user, I want to press `f` and type a substring, so that the listing narrows live to only entries whose name contains it.
2. As a user, I want to press `F` and type an extended regex, so that the listing narrows live to only entries whose name matches it, for patterns plain substring matching can't express (e.g. "ends with `.c` or `.h`").
3. As a user, I want the list to narrow with every keystroke, not just after I press Enter, so that I can see immediately whether my pattern is converging on what I'm looking for.
4. As a user, I want pressing Enter to commit the filter and drop me back into normal navigation, so that I can then move around, open, rename, delete, or yank within the narrowed list exactly as I would the full one.
5. As a user, I want the filtered view to persist after I commit it, so that "only show files containing X" is a lasting view change, not a one-off jump.
6. As a user, I want pressing Esc while composing a pattern to fully cancel the filter and return to the full listing, so that backing out of a filter I don't want is a single, predictable keystroke.
7. As a user, I want re-pressing `f`/`F` on an already-active filter to pre-fill the input with the current pattern, so that I can tweak or extend it instead of retyping it from scratch.
8. As a user, I want to clear an active filter by re-opening it, deleting the text down to empty, and pressing Enter, so that there's one consistent, discoverable way to get back to the full listing.
9. As a user, I want a pattern that matches nothing to show an empty listing, so that "no matches" is visually unambiguous rather than silently falling back to something else.
10. As a user composing a regex, I want an in-progress or malformed pattern (one that doesn't currently compile) to also show an empty listing, so that I never see a stale or partially-applied filter.
11. As a user composing a regex, I want the pattern text itself to render in red while it fails to compile and green once it's valid, so that I get instant feedback on whether what I've typed so far is even syntactically usable, independent of whether it currently matches anything.
12. As a user, I want `F`'s regex to use extended syntax (`grep -E`-style: `|`, `+`, `?`, `{}`, `()` all work without backslashes), so that quick patterns don't require remembering POSIX basic-regex escaping rules.
13. As a user, I want my regex to match anywhere within a filename by default (not require matching the whole name), so that `foo` matches `myfoo.txt` the same way plain `grep`/substring mode would, without me needing to write `.*foo.*`.
14. As a user, I want the filter to apply to directories exactly like files, so that the behavior is uniform and predictable — I don't have to remember a special exception for one entry type.
15. As a user, I want navigating into a subdirectory or up to the parent to always show the full, unfiltered listing there, so that a filter I set in one directory never silently leaks into a different one.
16. As a user, I want a filter I've committed to survive a same-directory refresh (e.g. after pasting a file, renaming, or deleting), so that an in-progress task doesn't get interrupted by losing my narrowed view.
17. As a user, I want up/down navigation to stay frozen (as it already is for every other text-entry mode) while I'm composing a filter pattern, so that the interaction stays consistent with Rename/Create/Run Command rather than introducing a special case.
18. As a user, I want to see on screen whether a filter is currently active and what it is, so that a short listing doesn't look like an empty or broken directory.
19. As a user, I want the yank status ("Yanked: X") to take priority over the filter-active indicator when both are true, so that I don't lose track of a pending copy/move because a filter status line replaced it.
20. As a user, I want to press Esc in normal navigation to cancel a pending yank, so that I have some way to back out of a copy/move I changed my mind about, instead of being forced to paste it or overwrite it with a different yank.
21. As a developer, I want filter matching and the filtered/sorted derivation to be pure functions with no filesystem or terminal I/O, so that they're covered by the same table-driven unit-testing convention already used for `entry_compare`/`sort_entries`/`is_hidden_name`.
22. As a developer, I want the new `Msg`/`Model` wiring to reuse the existing text-entry infrastructure (`edit_buf`/`edit_len`, the `text_entry` dispatch in `translate_event`, Esc/Enter/Backspace handling) rather than introducing a parallel input-handling path.

## Implementation Decisions

- **New `AppMode`**: `MODE_FILTER`, added to the `text_entry` set in `translate_event` alongside `MODE_RENAME`/`MODE_CREATE`/`MODE_RUN_CMD` — identical Esc/Enter/Backspace/printable-character routing, append-at-end/backspace-at-end only, no cursor movement, no live nav (up/down) while composing.
- **New `Msg`s**: `MSG_FILTER_PLAIN` (bound to `f`), `MSG_FILTER_REGEX` (bound to `F`), both unconditional in nav mode (don't depend on there being a selection or a non-empty directory), same tier as `MSG_RUN_CMD`.
- **`FilterType` enum**: `FILTER_NONE` / `FILTER_PLAIN` / `FILTER_REGEX`. One `AppMode` (`MODE_FILTER`) covers composing either pattern type; `Model` tracks which type is active/being composed via this field, not via two separate modes.
- **`Model` gains**:
  - `Entry unfiltered_entries[MAX_ENTRIES]` / `int unfiltered_count` — the master, per-directory-load copy of entries (post hidden-file filtering, as today, but pre-grep-filtering and unsorted). Populated on every `MSG_DIR_LOADED`.
  - `FilterType filter_type` and `char filter_pattern[NAME_MAX_LEN + 1]` — the **committed** filter, distinct from `edit_buf` (which is only the in-progress composition buffer, exactly as it already is for Rename/Create/Run Command).
  - The existing `entries`/`entry_count` fields keep their current contract unchanged: "the currently displayed, sorted set," consumed as-is by every render/nav/action codepath (move up/down, activate, rename, delete, yank, pagination). No index-mapping table is introduced.
- **New deep module — `apply_filter()`** (`helpers.c`/`helpers.h`): derives `entries`/`entry_count` from `unfiltered_entries`/`unfiltered_count` by running each name through `filter_matches()` and then the existing `sort_entries()` over the surviving subset. Called after every directory load and after every keystroke while composing (live). Because `entries` is always re-derived as a normal dense sorted array, every existing consumer needs zero changes.
- **New pure predicates** (`helpers.c`/`helpers.h`, alongside `is_hidden_name`/`is_protected_name`):
  - `filter_matches(name, type, pattern)` — `FILTER_NONE` always matches; `FILTER_PLAIN` uses `strstr`; `FILTER_REGEX` compiles `pattern` with `regcomp(..., REG_EXTENDED | REG_NOSUB)` and runs `regexec` unanchored (POSIX regex's default "search anywhere in the string" behavior — no special anchoring flags needed for `foo` to match `myfoo.txt`); a pattern that fails to compile matches nothing.
  - `filter_is_valid(type, pattern)` — used only for the compose-time red/green indicator; always true for `FILTER_PLAIN`, reflects `regcomp` success/failure for `FILTER_REGEX`.
- **Composing behavior** (`handle_edit`'s new `MODE_FILTER` cases): `MSG_TEXT_INPUT`/`MSG_DELETE` mutate `edit_buf` (as today) and then immediately call `apply_filter()` using `edit_buf` as the live pattern and the type from `MSG_FILTER_PLAIN`/`MSG_FILTER_REGEX` — narrowing happens on every keystroke. `MSG_ACTIVATE` copies `edit_buf`/type into `filter_pattern`/`filter_type` (committing) and returns to `MODE_NAV`, leaving `entries` as last computed. `MSG_CANCEL` resets `filter_type` to `FILTER_NONE`, clears `filter_pattern`, recomputes `entries` from `unfiltered_entries` (full listing, sorted), and returns to `MODE_NAV` — this is a full clear, not a revert to whatever was committed before this composing session started.
- **Entering filter mode** (`handle_nav`'s new `MSG_FILTER_PLAIN`/`MSG_FILTER_REGEX` cases): enters `MODE_FILTER`, pre-fills `edit_buf` with the current `filter_pattern` (regardless of which key was pressed, and regardless of the previously-committed type) so re-opening an active filter shows what's currently applied, ready to edit. Selection resets to index 0 on every recompute (top of the narrowed list) — no relocate-by-name, unlike `resort_and_relocate`.
- **Directory-load-time behavior**: `MSG_DIR_LOADED`'s handler populates `unfiltered_entries`/`unfiltered_count`, then calls `apply_filter()` using whatever `filter_type`/`filter_pattern` is currently committed — this is what makes the filter survive a same-directory reload (paste/rename/delete refresh) automatically, with no special-casing needed beyond "always reapply the committed filter on load." Any `CMD_LOAD_DIR` issued for a **different** path (`MSG_GO_PARENT`, `MSG_ACTIVATE` into a directory) resets `filter_type` to `FILTER_NONE` and clears `filter_pattern` before/alongside issuing the load, so the new directory always starts unfiltered.
- **Yank cancellation**: `handle_nav` gains a `MSG_CANCEL` case (previously absent — Esc in nav mode was a no-op) that clears `yank_path` when one is pending. This is bound to the same Esc key already used to cancel filter composition and every other edit mode; in nav mode it now does exactly one thing (clear a pending yank) rather than nothing.
- **Prompt line** (`view.c`'s `add_prompt_line`): a new `MODE_FILTER` case renders the live composition, `f`-style vim-prompt echo (mirroring `MODE_RUN_CMD`'s `:%s`) — e.g. `f:report` or `F:\.(c|h)$` — with the pattern text styled red while `filter_is_valid()` is false for a regex in progress, green otherwise (plain-mode patterns are always shown in the normal/valid style, since they have no invalid state).
- **Status line while browsing** (`add_prompt_line`'s default/nav-mode branch): currently shows `"Yanked: %s (%s)"` when a yank is pending, else blank. Extended to also show something like `"Filter: %s"` when `filter_type != FILTER_NONE` and no yank is pending — yank status takes priority over the filter indicator when both are true simultaneously.
- **Help text** (`HELP_TEXT` in `view.c`): gains `f: Filter`, `F: Filter (regex)`, and an update to the Esc/Backspace-area text to reflect Esc now cancelling a pending yank in nav mode.
- **No vendored dependency**: uses the standard library's POSIX `<regex.h>` (`regcomp`/`regexec`/`regfree`) directly — already available via glibc, same category as any other libc call, unlike `vendor/termbox2.h` which is vendored because it isn't part of libc.
- **No new `Cmd`**: filtering is entirely an in-memory `Model` transform over already-loaded data, like sort/group cycling — it never touches the filesystem beyond the directory load that already happens for other reasons.

## Testing Decisions

- Good tests here assert on pure function outputs and on `update()`'s returned `Model`, never on terminal rendering or real filesystem state — consistent with the testing conventions already established across the roadmap (`001-foundation.md` through `009-run-shell-command.md`).
- **`filter_matches`**: table-driven unit tests in `helpers_test.c`, same style as the existing `entry_compare`/`is_hidden_name` tests — cases for `FILTER_NONE` (always matches), plain substring match/no-match, regex match/no-match, regex matching mid-string (unanchored), and a malformed regex (matches nothing rather than crashing or erroring).
- **`filter_is_valid`**: table-driven, covering a valid regex, an invalid/incomplete regex (e.g. unterminated bracket expression), and `FILTER_PLAIN` (always valid regardless of content).
- **`apply_filter`**: table-driven, asserting the derived `entries`/`entry_count` for a given `unfiltered_entries` + filter type/pattern + sort/group mode — including the zero-matches case (empty output) and confirming the output is sorted per the existing `sort_entries` contract, not just filtered.
- **`update()`-level tests** (`update_test.c`, same harness as existing `MSG_RENAME`/`MSG_NEW`/`MSG_RUN_CMD` coverage):
  - `MSG_FILTER_PLAIN`/`MSG_FILTER_REGEX` from nav mode → `MODE_FILTER`, `edit_buf` pre-filled from any currently-committed `filter_pattern`.
  - Live recompute on `MSG_TEXT_INPUT`/`MSG_DELETE` while in `MODE_FILTER` → `entries`/`entry_count` reflect the narrowed set, `selected` reset to 0.
  - `MSG_ACTIVATE` in `MODE_FILTER` → commits `filter_type`/`filter_pattern`, returns to `MODE_NAV`, `entries` unchanged from last live computation.
  - `MSG_CANCEL` in `MODE_FILTER` → full clear (`filter_type` back to `FILTER_NONE`), `entries` restored to the full unfiltered/sorted set, returns to `MODE_NAV`.
  - `MSG_DIR_LOADED` with a currently-committed filter → new `unfiltered_entries` populated, `entries` re-derived with the filter still applied.
  - `MSG_GO_PARENT`/`MSG_ACTIVATE`-into-directory → filter reset to `FILTER_NONE` alongside the `CMD_LOAD_DIR` for the new path.
  - `MSG_CANCEL` in nav mode with a pending yank → `yank_path` cleared; with no pending yank → no-op, same `Model` returned.
- No scripted terminal/rendering tests for the red/green prompt coloring or the status-line priority logic — consistent with the existing "manual validation only" convention for anything that's purely a `view()` rendering concern rather than `update()` logic.

## Out of Scope

- **Case-insensitive matching** — both `f` and `F` are case-sensitive only in this PRD. Explicitly flagged as a good future addition, not built here.
- **Content-based search** ("real" `grep` over file contents) — this PRD is filename-only; the existing `:` Run Command mode (`009-run-shell-command.md`) already covers ad-hoc `!grep -rn ...` for content search.
- **Recursive/subdirectory search** — filtering only ever applies to the currently-loaded, single-directory listing, consistent with this app's flat, non-recursive browsing model.
- **Live navigation while composing** — up/down stay frozen while typing a filter pattern, matching every other text-entry mode; this is not fzf's "arrow keys work while you type" behavior.
- **Cursor movement or history within the filter buffer** — same append-at-end/backspace-at-end restriction, no recall of previous patterns, consistent with `009-run-shell-command.md`'s equivalent decisions for Run Command.
- **Reverting Esc-while-composing to the prior committed pattern** — Esc always fully clears, by design; there's no "undo my edits, keep what was there before" path.
- **A vendored regex engine** — deliberately not built; the standard library's `<regex.h>` covers this PRD's needs.
- **Basic (non-extended) POSIX regex support, or any other pattern dialect (glob, PCRE, etc.)** — `F` is extended-regex-only.

## Further Notes

Depends on `001-foundation.md` (`Model`/`Msg`/`Cmd` architecture, `update`/`view` split) and directly parallels the text-entry conventions established by `009-run-shell-command.md` (reusing `edit_buf`/`edit_len`, the `text_entry` dispatch, and Esc/Enter/Backspace semantics) and the load-time-filtering precedent set by `006-toggle-hidden-files.md` (this PRD's `unfiltered_entries`/`apply_filter` split is the same "filter close to the source, keep `entries` a single dense 1:1-indexed array" principle, extended to support live, keystroke-driven filtering that a load-time-only filter couldn't). The yank-cancellation piece (`MSG_CANCEL` in nav mode) is a small, previously-unaddressed gap that surfaced while resolving the filter/yank status-line display conflict, not a pre-existing backlog item — bundled into this PRD rather than filed separately since it reuses the same key (Esc) this PRD is already extending into nav mode.

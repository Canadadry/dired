---
title: "Recursive glob filter (plain / regex, across the directory tree)"
description: "Filter mode (g/G... now f/F) only narrows the current, single directory's listing — there's no way to find a file by name/pattern anywhere beneath the current directory without leaving dired."
status: needs-triage
---

## Problem Statement

`14-filter-mode.md` lets you narrow the *current* directory's listing by a
plain substring or regex, but this app has no recursive search at all.
Finding "the file matching `report` somewhere under this tree" still means
either drilling into subdirectories one at a time by eye, or dropping to
`:` Run Command and shelling out to `find`/`grep` yourself — which works,
but leaves you outside dired's normal navigation, selection, and action
model (rename/delete/yank/paste) entirely.

## Solution

`g` and `G` open a **recursive** counterpart to filter mode — same
plain-substring (`g`) / extended-POSIX-regex (`G`) matching, same live,
per-keystroke narrowing, same persistent-until-cleared/Esc-fully-clears
behavior as `14-filter-mode.md`, but sourced from every file and directory
under the current directory's subtree instead of just its direct children.

Because scanning the whole tree is comparatively expensive, it's split
into two steps: entering `g`/`G` first builds a **candidate list** — the
result of recursively walking `current_path` (via forking `find
<current_path> -mindepth 1 -print0`, no shell, output piped straight into
memory — no temp file, no terminal handoff, since nothing needs to page or
take over the screen) — capped at 4096 entries. *Then* the live
plain/regex matching from `14-filter-mode.md` runs against that
already-built list on every keystroke, exactly like flat filter mode does
against a directory's normal entries. This mirrors the existing
`unfiltered_entries`/`apply_filter` split from `14-filter-mode.md`: the
expensive part (building the source list) happens once; the cheap part
(matching) happens live.

Each matched entry displays its path relative to `current_path` (e.g.
`src/components/Foo.js`), not a bare filename, since matches can live at
any depth. Activating a matched file opens it via its real resolved path;
activating a matched directory navigates into it exactly like normal
directory activation — which, like any directory change, also resets both
flat-filter and glob state, since neither carries across a `current_path`
change.

Glob mode (`g`/`G`) and flat filter mode (`f`/`F`, from
`14-filter-mode.md`) are mutually exclusive: entering one clears the
other. There's no "glob-filtered-then-flat-filtered" stacking.

## User Stories

1. As a user, I want to press `g` and type a substring, so that I see every file/folder anywhere under the current directory whose name contains it, not just direct children.
2. As a user, I want to press `G` and type an extended regex, so that I get the same recursive reach as `g` but with pattern-matching power, exactly like `F` does for the flat listing.
3. As a user, I want the recursive candidate list to be built once when I enter `g`/`G`, with matching against it happening live on every keystroke afterward, so that I'm not re-scanning the whole tree on every character I type.
4. As a user, I want an empty pattern in glob mode to show an empty list (not everything), so that entering glob mode doesn't dump potentially thousands of candidates on screen before I've typed anything.
5. As a user, I want each matched entry to show its path relative to the directory I'm in, so that I can tell where in the tree a same-named file actually lives.
6. As a user, I want opening a matched file to launch it exactly as if I'd navigated to its real location and activated it there, so that glob results are fully usable, not just a preview.
7. As a user, I want opening a matched directory to navigate into it (leaving glob mode, since I'm now browsing a different `current_path`), so that a folder match works like any other directory activation.
8. As a user, I want the recursive walk to stop at 4096 candidates and tell me results are incomplete, so that pointing this at a huge tree doesn't hang the app or silently show a partial result with no warning.
9. As a user, I want that "results incomplete" notice to be a simple dismissible message, with the truncated (but still usable) candidate list already there once I dismiss it, so that hitting the cap doesn't throw away what *was* found.
10. As a user, I want renaming a matched file to rename it in place (same directory, new name) rather than relocating it to wherever I currently am browsing from, so that renaming a deeply nested match doesn't silently move it.
11. As a user, I want creating, deleting, renaming, or pasting while browsing glob results to refresh the candidate list (re-running the recursive walk), so that the results I'm looking at reflect what I just did rather than going stale.
12. As a user, I want `g`/`G` and `f`/`F` to be mutually exclusive, so that I'm never trying to reconcile a flat filter applied on top of a recursive one — I'm always in exactly one filtering mode or none.
13. As a user, I want re-pressing `g`/`G` on an already-active glob filter to pre-fill the input with the current pattern (matching flat filter mode's behavior), so that tweaking a glob pattern doesn't mean retyping it.
14. As a user, I want Esc while composing a glob pattern to fully clear glob mode back to the normal flat listing, exactly like Esc does for flat filter mode.
15. As a user, I want up/down navigation frozen while composing a glob pattern, matching every other text-entry mode in this app (flat filter included).
16. As a developer, I want the recursive-walk output parsing (splitting on `NUL`, capping at 4096, flagging truncation) to be a pure function fed a raw buffer, so that it's unit-testable without ever forking a real `find`.
17. As a developer, I want glob mode's live matching to reuse `14-filter-mode.md`'s `filter_matches`/`filter_is_valid` predicates unmodified, so that "what counts as a match" has exactly one implementation shared by both PRDs.
18. As a developer, I want the rename-target fix (rename into the entry's real containing directory, not blindly into `current_path`) implemented as a general fix rather than a glob-specific branch, so that it's correct for both flat and recursive entries with one code path.

## Implementation Decisions

- **New `AppMode`**: `MODE_GLOB`, added to the `text_entry` set in `translate_event` alongside `MODE_FILTER`/`MODE_RENAME`/`MODE_CREATE`/`MODE_RUN_CMD` — identical Esc/Enter/Backspace/printable-character routing and frozen-nav-while-composing behavior.
- **New `Msg`s**: `MSG_GLOB_PLAIN` (bound to `g`), `MSG_GLOB_REGEX` (bound to `G`), unconditional in nav mode, same tier as `MSG_FILTER_PLAIN`/`MSG_FILTER_REGEX`. Entering either while flat filter mode (`FILTER_NONE`/`FILTER_PLAIN`/`FILTER_REGEX`, from `14-filter-mode.md`) is active clears the flat filter first (mutual exclusion), and vice versa — flat filter's `MSG_FILTER_PLAIN`/`MSG_FILTER_REGEX` clears any active glob state.
- **`Model` gains**:
  - `GlobType glob_type` (`GLOB_NONE`/`GLOB_PLAIN`/`GLOB_REGEX`) and `char glob_pattern[NAME_MAX_LEN + 1]` — the committed glob filter, mirroring `filter_type`/`filter_pattern` from `14-filter-mode.md` but tracked separately (not unified into one field set, since empty-pattern semantics and the source list differ between the two modes).
  - `Entry glob_candidates[4096]` / `int glob_candidate_count` / `int glob_truncated` — the recursively-built candidate list, populated by `build_glob_candidates()`, analogous to `unfiltered_entries` but sourced from a recursive walk instead of a single `readdir()`.
- **New pure module — `split_nul_delimited()`** (`helpers.c`/`helpers.h`): takes a raw byte buffer and its length (as produced by reading a `find ... -print0` child's stdout), splits on `NUL` bytes into up to a caller-supplied max count of relative-path strings, `stat()`s each resolved path to populate an `Entry` array, and sets an output flag when the buffer contained more entries than the max. Pure aside from the per-entry `stat()` calls (consistent with how `load_directory()` already mixes pure parsing with `stat()` per entry) — testable by feeding it a canned in-memory buffer, no need to fork a real `find`.
- **`apply_filter()` extended** (from `14-filter-mode.md`): gains an `int empty_matches_all` parameter. Flat filter mode (`14-filter-mode.md`) passes `true` (unchanged behavior: empty pattern shows everything). Glob mode passes `false` (empty pattern shows nothing). Both modes otherwise share the exact same predicate-loop-plus-`sort_entries()` implementation — no duplicated matching logic between the two PRDs.
- **New `execute_*`-style function — `build_glob_candidates(cwd)`** (`dired.c`, not unit tested, same convention as `execute_preview`/`execute_run_cmd`/`run_piped`): forks/execs `{"find", cwd, "-mindepth", "1", "-print0", NULL}` via `execvp` (argv, no shell — consistent with this app's "never a shell for constructed commands" convention; `:` Run Command's `sh -c` exception doesn't apply here since this command is built by the app, not typed by the user), redirects the child's stdout into a pipe, reads the full output into a heap buffer in the parent, waits on the child, then calls `split_nul_delimited()` on the result. No `tb_shutdown()`/`tb_init()` around this — unlike every other fork/exec in this app (editor, pager, hexdump, `:` Run Command), nothing here writes to the terminal or needs to page, so the terminal is never handed over; this is the first "silent" fork/exec in the codebase. Runs synchronously, blocking the main loop like every other `Cmd` — no async/progress indicator.
- **4096 cap and truncation**: if `split_nul_delimited()`'s truncation flag is set after a `build_glob_candidates()` call, `update()` sets `mode = MODE_ERROR` with a message to the effect of "results incomplete (4096+ matches) — try a subfolder," with `glob_candidates`/`glob_candidate_count` already populated. Dismissing the error (any key, existing `MODE_ERROR` behavior) returns to `MODE_NAV`... immediately followed by entry into `MODE_GLOB` composing against the truncated candidate list, since the error only ever fires as part of entering/rebuilding glob mode.
- **Rename fix** (`update.c`): renaming no longer always targets `join_path(current_path, edit_buf)`. It now targets `join_path(dirname_of(entries[selected].name), edit_buf)`, resolved against `current_path` — a new pure `dirname_of()` helper returns `current_path` unchanged when the entry's `name` contains no `/` (every flat-listing case, including `14-filter-mode.md`'s flat filter), and the parent segment when it does (any glob-mode match). This is a strict generalization, not a new special case — it changes behavior only for names containing `/`, which never occurs outside glob mode today.
- **Mutation-triggered rebuild**: `MSG_OP_SUCCEEDED` (already the trigger for reloading after paste/rename/delete/create) is extended so that when `glob_type != GLOB_NONE`, instead of (or in addition to) the normal `CMD_LOAD_DIR` for `current_path`, it also re-issues `build_glob_candidates(current_path)` and re-runs `apply_filter()` with the current `glob_pattern`/`glob_type` — staying in `MODE_GLOB`/glob-filtered view rather than dropping back to a flat listing. This is the expensive path (full tree re-walk) but only fires on an actual mutation, never per-keystroke.
- **Directory navigation resets glob state**: any `CMD_LOAD_DIR` issued for a **different** path (`MSG_GO_PARENT`, activating into a directory — including a matched directory from glob results itself) resets `glob_type` to `GLOB_NONE` and clears `glob_pattern`/`glob_candidates`, exactly mirroring `14-filter-mode.md`'s equivalent flat-filter reset.
- **Display**: matched entries render `entries[i].name` as-is (already the `current_path`-relative path with slashes, as produced by `split_nul_delimited()`), so `add_entry_line()` in `view.c` needs no change — it already just prints `Entry.name` verbatim.
- **Prompt line / help text**: `add_prompt_line()` gains a `MODE_GLOB` case mirroring `MODE_FILTER`'s (`g:pattern/G:pattern`, red/green regex-validity coloring for `G`). Status line shows glob-active state the same way and with the same yank-priority ordering established in `14-filter-mode.md`. `HELP_TEXT` gains `g: Glob` / `G: Glob (regex)`.
- **No vendored dependency, no new external tool beyond `find`**, which this app already assumes availability of transitively (it's as standard a POSIX tool as `sh`/`more`, already relied on by `009-run-shell-command.md`'s own examples).

## Testing Decisions

- Good tests here assert on pure function outputs and `update()`'s returned `Model`, never on terminal rendering, real filesystem state, or by forking a real `find` — consistent with `14-filter-mode.md` and every prior PRD's testing conventions.
- **`split_nul_delimited`**: table-driven, fed canned in-memory buffers — a buffer under the cap (all entries parsed, `truncated` false), a buffer at exactly the cap, a buffer over the cap (only the first N parsed, `truncated` true), an empty buffer (zero entries), and a buffer with a trailing/leading `NUL` (no empty-string entries produced).
- **`apply_filter`'s new `empty_matches_all` parameter**: extends the existing `14-filter-mode.md` table-driven tests with cases for both `true` (existing behavior, unchanged) and `false` (empty pattern → empty output) against the same fixture data.
- **`dirname_of`**: table-driven — a bare name (no `/`, returns `current_path` unchanged), a one-level nested name (`src/foo.c` → `current_path/src`), a multi-level nested name, and an edge case of a name that's just `/`-prefixed oddly (defensive, shouldn't occur given `find`'s output format but worth a table row).
- **`update()`-level tests** (same harness as `14-filter-mode.md`'s):
  - `MSG_GLOB_PLAIN`/`MSG_GLOB_REGEX` from nav mode → `MODE_GLOB`, and clears any active flat-filter state (`filter_type` back to `FILTER_NONE`).
  - `MSG_FILTER_PLAIN`/`MSG_FILTER_REGEX` while glob mode is active → clears `glob_type` back to `GLOB_NONE` (the reverse direction of mutual exclusion).
  - Rename target computation for a nested `entries[selected].name` vs. a flat one, confirming the `join_path(dirname_of(...), edit_buf)` result in both cases.
  - `MSG_OP_SUCCEEDED` while `glob_type != GLOB_NONE` → triggers a candidate rebuild rather than a flat `CMD_LOAD_DIR`, staying in glob-filtered view.
  - Directory-navigation `Msg`s reset `glob_type`/`glob_pattern`/`glob_candidates` alongside the existing flat-filter reset.
  - The 4096-cap path: a `build_glob_candidates` result with `truncated` set → `Model.mode == MODE_ERROR` with the candidate list already populated.
- **`build_glob_candidates`** itself (fork/exec/pipe/wait) is explicitly not unit tested, matching the established convention for `execute_preview`/`execute_run_cmd`/`run_piped` — validated manually: a small tree (well under 4096), a tree that exceeds 4096 (confirming the truncation notice and that the truncated list is still usable after dismissal), a tree containing symlinks (confirming `find`'s default non-`-L` behavior — symlinks appear as themselves, not followed/descended into, so no infinite loop risk), and a tree with nested directories at multiple depths (confirming relative-path display and that activating a nested directory match correctly navigates and resets glob/filter state).

## Out of Scope

- **Depth limits, name-based exclusion lists (`node_modules`, `.git`, etc.), or `.gitignore` awareness** — explicitly rejected as not this app's scope; the only bound on recursion is the flat 4096-candidate cap.
- **Stacking flat filter on top of glob results** — the two modes are strictly mutually exclusive in this PRD.
- **Async/streaming candidate building or a progress indicator** — `build_glob_candidates` runs synchronously and blocks the main loop like every other `Cmd`, consistent with `009-run-shell-command.md`'s equivalent decision.
- **Live navigation while composing, cursor movement/history in the pattern buffer, case-insensitive matching** — all out of scope for the same reasons given in `14-filter-mode.md`, inherited unchanged here.
- **Following symlinks during the recursive walk** (`find -L`) — default (non-following) `find` behavior is used, avoiding symlink-loop risk; not configurable in this PRD.
- **A temp file on disk for the candidate list** — considered and explicitly rejected in favor of piping the walk's output straight into memory, both for simplicity and to avoid the predictable-filename/symlink-race risk a composed `/tmp` path would introduce.
- **Rebuilding the candidate list on mere re-entry into glob mode** (pressing `g`/`G` again without an intervening mutation) — the existing in-memory candidate list is reused; only an actual mutation (create/delete/rename/paste) triggers a rebuild.

## Further Notes

Depends on `001-foundation.md` and directly extends `14-filter-mode.md` —
reuses its `filter_matches`/`filter_is_valid` predicates and its
`MODE_FILTER`/text-entry/persistent-filter/Esc-fully-clears interaction
model verbatim, differing only in: pattern source (recursive walk vs.
single `readdir()`), empty-pattern semantics (empty vs. everything),
display format (relative path vs. bare name), a cap-driven error path, and
rebuild-on-mutation instead of the flat PRD's simple reload-and-reapply.
Should be implemented after `14-filter-mode.md` ships, both because it
depends on `filter_matches`/`filter_is_valid`/`apply_filter` existing
already and because the mutual-exclusion wiring needs `filter_type` to
exist as a real `Model` field to reset against.

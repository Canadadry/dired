---
title: "Glob mode: walk and match inline, drop the raw-candidate cache"
description: "Glob mode's two-phase cache-then-filter design silently drops files from ever becoming match candidates once a subtree has more than 4096 entries, and throws a blocking error the moment a pattern matches more than 1024 of what it did keep."
status: done
---

## Problem Statement

`011-glob-mode.md` built glob (`g`/`G`) as two phases: entering glob mode
forks `find <cwd> -mindepth 1 -print0` once, `stat`s every result, and
caches up to 4096 entries (`GLOB_MAX_CANDIDATES`) as `glob_candidates`.
Every keystroke afterward filters that cache in memory
(`recompute_glob_live`), capped at 1024 (`MAX_ENTRIES`) for display.

This breaks in a directory tree with a lot of files, in two separate ways:

1. If the subtree has more than 4096 filesystem entries total, everything
   past the 4096th one `find` happens to emit is dropped from the cache
   before any pattern is even applied. A real match past that point can
   never appear, for any pattern — a silent, invisible failure.
2. If your pattern matches more than 1024 of whatever *did* make it into
   the cache — plausible on any real project, e.g. a common substring
   across a large repo — `012-fix-glob-filter-scroll-yank-bugs.md`'s fix
   for the resulting buffer overflow routes you into a blocking
   `MODE_ERROR` ("too many matches — narrow your pattern") instead of just
   showing what it found. This is the failure actually being hit in
   practice.

Both failures trace back to the same design: a fixed-size raw cache
(4096) feeding a fixed-size display cap (1024), with two different
truncation points and two different ways of surfacing them.

## Solution

Collapse the two phases into one. Glob mode still commits a pattern and
walks the tree to find matches, but the walk and the matching happen
together, in-process, directly against the current pattern — recursively
walking the directory tree, testing each entry's path (relative to the
directory the walk started from) against the pattern as it's encountered,
and keeping only the first 1024 matches, stopping the walk the instant
that cap is hit. There is no longer a separate, larger raw-candidate
cache to overflow or silently truncate — the only cap that exists is the
one number that was always the real limit: how many matches can be shown
at once.

Because walking and matching are now the same expensive step, it no
longer runs on every keystroke (that would mean re-walking the tree on
every character typed). Composing a pattern is decoupled from running the
search: typing edits the pattern text only; the file list underneath
stays exactly as it was (the previous search's results, or empty the
first time) until Enter commits the pattern and triggers one walk. The
same walk also re-runs automatically after a successful file operation
while a glob is active, exactly as it does today, so results don't go
stale after a rename/delete/paste.

Hitting the 1024-match cap is no longer an interruption. It's expected
behavior for a broad pattern over a big tree, not an error: the list
simply shows the first 1024 matches found (sorted per the active sort
mode, same as always), with a status-line hint that results are capped —
no blocking screen, no re-entry dance.

## User Stories

1. As a user, I want a glob pattern to be able to match a file anywhere in a large subtree, so that a directory with a lot of files doesn't silently make some files unfindable no matter what I type.
2. As a user, I want a pattern that matches more than 1024 files to just show me the first 1024, so that a broad pattern over a big tree doesn't interrupt me with an error I have to dismiss before I can keep working.
3. As a user, I want a status-line hint when results are capped at 1024, so that I know I'm not looking at every match and can narrow my pattern if I need the rest.
4. As a user, I want typing a glob pattern to not touch the filesystem on every keystroke, so that composing a pattern character-by-character stays instant regardless of how big the tree is.
5. As a user, I want the file list to only update once I press Enter on a glob pattern, so that I'm not staring at a list that's re-shuffling mid-thought while I'm still typing.
6. As a user, when I press Enter on a glob pattern, I want exactly one search of the tree to run using what I just typed, so that the results I see correspond to the pattern I actually committed to.
7. As a user, re-entering glob mode on an already-committed pattern, I want to still see the last results underneath while I edit, so that I have context for what I'm changing before I hit Enter again.
8. As a user, the very first time I enter glob mode with no pattern committed yet, I want the list to show nothing until I've typed something and pressed Enter, so that I'm not dumped into a huge, meaningless listing.
9. As a user, I want a glob pattern to match against a file's full path relative to the directory I searched from (e.g. `sub/report.txt`), not just its bare filename, so that I can find files by their location as well as their name, exactly like today.
10. As a user, I want creating, deleting, renaming, or pasting while browsing glob results to refresh the search against the currently-committed pattern, so the results I'm looking at reflect what I just did rather than going stale.
11. As a user, I want the recursive walk to skip a subdirectory I don't have permission to read rather than failing the whole search, so that one locked-down folder somewhere in the tree doesn't break glob everywhere else.
12. As a user, I want the walk to not follow symlinked directories (list the symlink itself, don't descend through it), so that a symlink cycle somewhere in the tree can't hang or crash the search.
13. As a user, I want hidden files and directories included in glob results, matching today's behavior, so that switching to this new walk doesn't quietly change what's searchable.
14. As a user, I want `g`/`G` (plain vs. regex) to behave identically in every way described above, so the walk/match/cap behavior isn't a plain-only or regex-only fix.
15. As a developer, I want the matching predicate itself (`filter_matches`) to stay exactly as it is today and be reused unchanged, so "what counts as a match" still has exactly one implementation shared with flat filter mode.
16. As a developer, I want the recursive walk's early-stop-at-1024 behavior to actually stop recursing (not just stop appending to a list while the walk keeps running), so that hitting the cap on a huge tree is fast, not just bounded in memory.
17. As a developer, I want the dead two-phase machinery (the 4096 raw cache, its truncation error path, `split_nul_delimited`, the glob-specific error-reentry flag) fully removed rather than left dormant, so the codebase doesn't carry two competing explanations for how glob works.

## Implementation Decisions

- **New walk executor — `walk_glob_matches()`** (`dired.c`, same category as `load_directory()`/`execute_preview()`: does real filesystem I/O, not unit-tested): replaces `execute_build_glob()` and glob's use of `split_nul_delimited()`. Recursively walks a starting directory via `opendir`/`readdir`, threading the accumulated path (relative to the walk's root) through the recursion in a `PATH_MAX_LEN`-bounded buffer. For each entry: `lstat`s it, calls the existing `filter_matches()` against the accumulated relative path with the committed pattern/type, and if it matches, appends it (with its `struct stat`) to a caller-supplied buffer capped at `MAX_ENTRIES` (1024). Only recurses into entries where `lstat` reports a directory (never follows symlinks, matching `find`'s default and incidentally ruling out symlink cycles). Skips `.`/`..`. Does not skip dotfiles/dot-directories otherwise (matches today's `find -mindepth 1`, no hidden-file filtering). A subdirectory that fails to `opendir` (permission denied) is skipped silently; the walk continues elsewhere in the tree. The walk returns (stops recursing entirely, not just stops appending) the moment the output buffer is full. Sets an output flag when it stopped due to hitting the cap (vs. exhausting the tree with room to spare), for the status-line hint.
- **Trigger point moves from mode-entry to pattern-commit**: `MSG_GLOB_PLAIN`/`MSG_GLOB_REGEX` (`g`/`G`) no longer issue `CMD_BUILD_GLOB`. They only set `mode`/`glob_type`, prefill `edit_buf` from any already-committed `glob_pattern`, and — only when there was no prior committed glob (`glob_type` was `GLOB_NONE` going in) — reset `entries`/`entry_count` to empty, since there's nothing committed yet to keep showing. `MSG_ACTIVATE` while in `MODE_GLOB` is the new dispatch point: it commits `glob_pattern = edit_buf`, exits edit mode, and issues `CMD_BUILD_GLOB` carrying the committed pattern and type.
- **Live-typing recompute removed**: `MSG_TEXT_INPUT`/`MSG_DELETE` while in `MODE_GLOB` only mutate `edit_buf` (the prompt line still updates live, showing what's being typed and its regex-validity coloring via the existing `filter_is_valid`) — they no longer call any recompute function. `recompute_glob_live()` is deleted.
- **`Cmd` gains fields for `CMD_BUILD_GLOB`**: a `GlobType` field, and the pattern text carried in the existing `cmd_text` buffer (already present on `Cmd`, used today by `CMD_RUN`, unused by `CMD_BUILD_GLOB` — no new string field needed).
- **`handle_glob_built` simplifies to a direct copy**: `MsgGlobBuilt`'s `entries`/`entry_count` (already-matched, already-capped-at-1024 results from the walk) are copied straight into `Model.entries`/`entry_count`, exactly like `handle_dir_loaded` does for a normal listing — no `apply_filter` call, since matching already happened during the walk. `msg.glob_built.truncated` (kept, same field, meaning shifted from "raw walk hit 4096" to "match walk hit 1024 and stopped early") sets a new `Model.glob_capped` flag instead of entering `MODE_ERROR`.
- **Status-line hint, not an error**: the existing default-case status line (`add_prompt_line` in `view.c`, the branch that renders `"Glob: %s"` when a glob is committed and you're not actively composing) appends a capped indicator when `glob_capped` is set (e.g. `"Glob: report (1024+ shown)"`). No new `AppMode`, no interruption.
- **`MSG_OP_SUCCEEDED` unchanged in structure**: still re-issues `CMD_BUILD_GLOB` when `glob_type != GLOB_NONE`, now just also carrying the committed pattern/type so the walk executor has what it needs.
- **Full removal**: `GLOB_MAX_CANDIDATES`, `Model.glob_candidates`, `Model.glob_candidate_count`, `Model.glob_truncated`, `Model.error_reenter_glob` and its re-entry branch in the `MODE_ERROR` handler, `enter_glob_match_truncated_error()`, `split_nul_delimited()` and its only call site. Generic op-failure errors (rename/copy/move/delete/glob-`opendir`-failure/OOM) keep using plain `MODE_ERROR` with no glob-reentry special case, since nothing sets that flag anymore.
- **Matching semantics unchanged**: `filter_matches()` itself is not modified — case-sensitive substring (`FILTER_PLAIN`) or unanchored POSIX-extended regex (`FILTER_REGEX`) against the full relative path, exactly as today.

## Testing Decisions

Good tests here assert on pure function outputs and on `update()`'s
returned `Model`/`Cmd`, never on terminal rendering or real filesystem
state — the convention already established across every prior PRD's
Testing Decisions section.

- **`walk_glob_matches()` itself is not unit-tested**, matching the
  established convention for `load_directory()`/`execute_preview()`/
  `execute_run_cmd()` (real filesystem I/O, no canned-buffer seam to test
  against). Validated manually against a scratch directory tree covering:
  a plain match and a regex match at multiple depths; a pattern matching
  more than 1024 files (confirms the walk actually stops early rather
  than just capping what it appends — observable via wall-clock time on a
  large synthetic tree); a symlinked directory present in the tree
  (confirms it's listed but not descended into); a subdirectory with
  permissions removed (confirms the walk skips it and still finds matches
  elsewhere); a dotfile and a dot-directory with matching contents
  (confirms both are still reachable, no hidden-file filtering).
- **`update()`-level tests** (`update_test.c`, same harness already used
  for glob/filter tests): rewrite the existing glob suite to match the
  new shape —
  - `MSG_GLOB_PLAIN`/`MSG_GLOB_REGEX` never produce `CMD_BUILD_GLOB`
    (replaces the old need-build/reuse-candidates cases).
  - First-time entry (`glob_type` was `GLOB_NONE`) clears `entries`;
    re-entry on an already-committed glob leaves `entries` untouched.
  - `MSG_ACTIVATE` in `MODE_GLOB` commits `glob_pattern`, exits edit mode,
    and produces `CMD_BUILD_GLOB` carrying the committed pattern/type.
  - `MSG_TEXT_INPUT`/`MSG_DELETE` in `MODE_GLOB` update `edit_buf` only —
    `entries`/`entry_count` unchanged (replaces
    `test_glob_live_recompute_on_text_input`).
  - `MSG_GLOB_BUILT` copies `entries`/`entry_count` directly into the
    model and sets `glob_capped` from the message's truncated flag — no
    `MODE_ERROR` transition (replaces
    `test_glob_built_truncated_enters_error_with_candidates_populated`
    and `test_glob_truncation_error_dismiss_reenters_glob_composing` from
    `012-fix-glob-filter-scroll-yank-bugs.md`).
  - `MSG_OP_SUCCEEDED` while `glob_type != GLOB_NONE` still produces
    `CMD_BUILD_GLOB`, now asserting the pattern/type are carried on the
    `Cmd`.
  - Directory-navigation `Msg`s still reset `glob_type`/`glob_pattern`
    (unchanged from today), no `glob_candidates` to reset anymore.
- **`split_nul_delimited`'s existing test is deleted** along with the
  function.
- **`view_test.c`**: extend the existing glob prompt-line coverage with a
  case asserting the capped-results status-line hint appears when
  `glob_capped` is set and doesn't when it isn't.

## Out of Scope

- **Any change to `filter_matches`/`filter_is_valid` matching semantics**
  — unchanged; only where/when matching runs changes.
- **Async/streaming/progress indication for the walk** — it still runs
  synchronously and blocks the main loop like every other `Cmd`. Making
  long-running commands non-blocking is `11-async-action-queue.md`'s
  concern, not this PRD's.
- **Debounced or partial live-typing feedback** (e.g. re-searching after
  a pause in typing, or showing a subset of results while composing) —
  explicitly rejected; the walk runs exactly once, on Enter, or on a
  post-op refresh.
- **Configurable or higher-than-1024 cap, or a way to page/see past the
  first 1024 matches** — the cap stays exactly at `MAX_ENTRIES`, matching
  what the rest of the app already displays at once; narrowing the
  pattern is the only way to see past it.
- **Following symlinks during the walk, depth limits, name-based
  exclusion lists (`node_modules`, `.git`), or `.gitignore` awareness** —
  none of these were in scope for `011-glob-mode.md` either and remain
  unaddressed here.
- **Changing flat filter mode (`f`/`F`) in any way** — this PRD only
  touches glob mode; flat filter's live-per-keystroke behavior is
  intentionally left as-is.

## Further Notes

Supersedes the candidate-cache portions of `011-glob-mode.md` (the
`find`-fork/pipe/`split_nul_delimited` build step, the 4096 cap, the
two-phase cache-then-filter split) and the glob-side truncation handling
added by `012-fix-glob-filter-scroll-yank-bugs.md` (`glob_truncated`,
`error_reenter_glob`, the "too many matches" `MODE_ERROR` path). Everything
`011-glob-mode.md` established that isn't explicitly changed here —
mutual exclusion with flat filter, directory-navigation resetting glob
state, the rename-target fix from `012`, matched-entry display as a
relative path, activating a matched file/directory — carries forward
unchanged.

The move to an in-process recursive walk was chosen specifically to allow
early-exit at the 1024-match cap without the complexity of killing a
still-running forked `find` subprocess mid-stream, and to keep exactly
one number (`MAX_ENTRIES`) as the only capacity concept glob mode has,
rather than two (a raw-fetch cap and a separate display cap) that could
disagree with each other.

---
title: "Color entries by git status"
description: "Every entry renders identically regardless of whether it's modified, untracked, ignored, or conflicted under git, so the listing gives no signal about what's changed."
status: needs-triage
---

## Problem Statement

The listing shows permissions, size, and name as plain text with no
indication of git status. A user working in a repo has to leave dired and
run `git status` separately to know which files or folders have
uncommitted changes, are untracked, or are ignored.

## Solution

Color every entry — files and directories alike — by its git status, using
the same semantic style-tag mechanism the codebase already uses for
selection (`view()` emits a `StyleTag`; `main()`/`dired.c`'s `style_colors()`
is the only place that maps a tag to real termbox2 colors). A file is
colored by its own direct git status. A directory is colored by a
recursive aggregate of everything inside it, so a folder several levels
above a single modified file still shows that something changed below it.

## User Stories

1. As a user, I want a file with unstaged or staged changes to render in a distinct color, so that I can spot what I've edited without leaving the listing.
2. As a user, I want an untracked file to render in its own distinct color, so that I can spot new files I haven't added to git yet.
3. As a user, I want an ignored file to render in its own distinct, muted color, so that I can tell at a glance it's intentionally excluded from git rather than just clean.
4. As a user, I want a file with a merge conflict to render in its own distinct, attention-grabbing color, so that I can find conflicts quickly.
5. As a user, I want a folder containing changes anywhere below it — not just directly inside it — to be colored, so that I don't have to open every subdirectory to find out something changed.
6. As a user, when a folder contains a mix of different git states among its descendants, I want it colored by the single most important one (conflicted, then modified, then untracked, then ignored, then clean), so that the color always points me at the thing most worth my attention.
7. As a user, I want a clean file or folder — fully committed, no changes — to render exactly as it does today, so that the common case stays visually quiet and only exceptions stand out.
8. As a user, I want a folder or file outside of any git repository (or a parent directory above the repo root) to render exactly as it does today, so that non-repo browsing isn't cluttered with a "not applicable" indicator.
9. As a user, I want the selected row to still be visually distinguishable as "selected" even when it's also colored by git status, so that I never lose track of my cursor position.
10. As a user, I want a selected git-colored row to also still show which git state it's in, so that selecting an entry doesn't hide the very information the coloring exists to show.
11. As a user, I want colors to update automatically after I navigate into a different directory, so that I don't have to trigger a manual refresh.
12. As a user, I want colors to update automatically after I create, delete, rename, paste, or edit a file in the current directory, so that the listing never shows stale git state after an action I just took.
13. As a user, when git isn't installed or the current directory isn't part of a repository, I want the listing to simply render without any git coloring (same as today), so that a missing/inapplicable git integration never surfaces an error or broken state.
14. As a user, I want a symlink that points at a directory to be colored by its own git status only, so that following it into a possibly unrelated part of the filesystem never happens just to compute a color.
15. As a developer, I want the git-status classification (parsing `git status` output and aggregating it into a per-entry tag) to be a pure function with no filesystem or process access, so that every priority-ordering and aggregation rule can be verified without a real repo or subprocess.
16. As a developer, I want `view()` to remain the single place deciding which semantic tag an entry line gets, and `style_colors()` in `dired.c` to remain the single place mapping a tag to actual fg/bg colors, so that this feature slots into the existing architecture rather than introducing a second styling mechanism.

## Implementation Decisions

- **Scope:** both files and directories are colored, driven by git status. Files use their own direct status; directories use a recursive aggregate of all descendants (not just direct children).
- **States tracked, in priority order (highest wins when a directory's descendants mix states):** Conflicted → Modified (staged and unstaged merged into one state) → Untracked → Ignored → Clean. Clean and "not in a repo" are the same rendering outcome (today's default, uncolored).
- **Colors:** Conflicted = red, Modified = green, Untracked = yellow, Ignored = dim/dark gray, Clean = default (no color change).
- **New module `gitstatus` (pure, no I/O):** takes the raw text of `git status --porcelain --ignored -uall` plus the current directory's entry names, and returns a `GitStatusTag` (`GIT_STATUS_NONE`, `GIT_STATUS_IGNORED`, `GIT_STATUS_UNTRACKED`, `GIT_STATUS_MODIFIED`, `GIT_STATUS_CONFLICTED`, ordered so a numeric max also gives the priority winner) for each entry. Files match a porcelain line by exact relative path; directories match by prefix against every porcelain line and take the highest-priority match found. Symlinks are matched like any other entry (by their own path) and are never treated as a prefix to descend through.
- **`loaddir` (modified):** after listing entries via `readdir`/`lstat` as it does today, runs `git -C <path> status --porcelain --ignored -uall` via `popen`, and — if that succeeds — hands the output to `gitstatus` to fill in each `Entry`'s new status field. If `popen`/`git` fails for any reason (not a repo, git not installed, non-zero exit), every entry is simply left at `GIT_STATUS_NONE`; no error is surfaced, matching how a non-repo directory already renders today.
- **`model.h` (modified):** `Entry` gains a `GitStatusTag git_status` field; the `GitStatusTag` enum is defined here alongside `Entry`.
- **`view.h`/`view.c` (modified):** `StyleTag` gains 8 new values — one pair (plain and "_selected") for each of Conflicted/Modified/Untracked/Ignored. `add_entry_line()` and `add_virtual_line()` pick among these (or fall back to today's `STYLE_NORMAL`/`STYLE_SELECTED` for `GIT_STATUS_NONE`) based on the entry's `git_status` and whether it's the selected row — the same place that already decides `STYLE_NORMAL` vs `STYLE_SELECTED` today.
- **`dired.c` (modified):** `style_colors()` gains the fg/bg mapping for the 8 new tags. Unselected: git color as foreground, default background. Selected: the "_selected" variants swap this — the git color becomes the background, with black or white foreground (whichever contrasts) — the same reverse-video idea `STYLE_SELECTED` already uses with white, just parameterized by the git color instead.
- **Recompute triggers:** the git status subprocess runs every time `CMD_LOAD_DIR` executes — which already happens on navigation (`MSG_GO_PARENT`, entering a directory) and after every successful file operation (`MSG_OP_SUCCEEDED` already triggers `CMD_LOAD_DIR` today), per `update.c`. No new `Cmd`/`Msg` types are introduced; this piggybacks entirely on the existing reload cycle.
- **Performance:** a single `git status --porcelain --ignored -uall` subprocess call per directory load/reload (not one call per entry), run synchronously and blocking, matching every other filesystem operation in this codebase. Accepted as the simplest correct implementation; no async/background variant is introduced.
- **Help text:** unchanged — no color legend is added to the help bar or elsewhere.

## Testing Decisions

- `gitstatus`'s classification/aggregation logic gets heavy table-driven unit tests (new `test/gitstatus_test.c`, same `Case[]` + loop style as `test/helpers_test.c`): canned porcelain-output blobs paired with entry name lists, covering each of the 5 states individually, priority ordering when a directory's descendants mix states, recursive aggregation through nested subdirectories, a file that exact-matches a porcelain path, and a directory-vs-symlink-with-the-same-name-prefix case confirming symlinks aren't treated as a prefix to descend through.
- `view()`'s tag-selection logic (choosing the right `StyleTag` from an entry's `git_status` and selection state) extends the existing `test/view_test.c` table-driven pattern (e.g. `test_nav_listing`), the same way it already asserts `STYLE_SELECTED` vs `STYLE_NORMAL` today.
- `loaddir`'s `popen`/subprocess plumbing is not unit tested, matching how its existing `readdir`/`lstat` filesystem calls aren't mocked or unit tested today — this is the impure I/O boundary, left to manual/integration verification.
- `style_colors()` in `dired.c` is not unit tested, matching the existing untested status of its `STYLE_SELECTED`/`STYLE_PROMPT`/`STYLE_ERROR` mappings today.

## Out of Scope

- A color legend in the help bar or anywhere else in the UI.
- Any indication that git integration is inactive (missing `git`, not a repo) beyond simply rendering uncolored, same as today.
- Following symlinked directories to aggregate status through them.
- Splitting "Modified" into separate staged vs. unstaged colors.
- User-configurable color palettes (e.g. respecting `LS_COLORS` or a config file) — this codebase has no persistence/config layer and none is introduced here.
- An async/background/non-blocking variant of the git status call.
- Special-casing submodules, or descending into a nested repo's own `.git` — a dirty submodule is reported by the outer repo's `git status` as a single line, same as git's own default behavior.

## Further Notes

Depends on `01-foundation` (the `Model`/`Msg`/`Cmd`/`update()`/`view()` architecture) and reuses the same semantic-style-tag extension mechanism `01-foundation` already established for `STYLE_SELECTED`/`STYLE_PROMPT`/`STYLE_ERROR`.

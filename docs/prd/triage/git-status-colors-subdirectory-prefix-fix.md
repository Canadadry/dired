---
title: "Git status colors: fix coloring below the repo's top-level directory"
description: "Git status colors stop propagating as soon as you navigate below the repo's top-level directory, because git always reports changed-file paths relative to the repo root, not to the directory dired queried."
status: needs-triage
---

## Problem Statement

The "color entries by git status" feature (PRDs 017-021, all shipped) colors
entries correctly when browsing the top level of a git repository, but the
moment the user navigates into a subdirectory, coloring stops working
entirely: files and directories that are genuinely modified, new, or deleted
render with no git color at all, identical to a clean file.

Concretely, given a repo `a` containing `a/b/c/d.md`, if `d.md` is edited:
`a`'s own listing (viewed from `a`'s parent) correctly shows `a` colored to
reflect the change somewhere below it, but once the user opens `a` and
descends into `b`, then `c`, none of `b`, `c`, or `d.md` render with any git
color — they appear identical to an untouched repo.

## Solution

The root cause: `git status --porcelain` always reports changed-file paths
relative to the repository's top-level directory, never relative to whatever
directory git was invoked from (confirmed by direct reproduction — running
`git status --porcelain --ignored` from the repo root, from a subdirectory,
and from a further-nested subdirectory all produced the identical
root-relative path for the same changed file). `loaddir`'s git invocation is
scoped to whichever directory dired just loaded, but `gitstatus`'s classifier
matches the paths it gets back against entry names local to that loaded
directory. This only lines up when the loaded directory is the repo root;
below the root, the reported path (e.g. `b/c/d.md`) no longer matches the
entry name being colored (e.g. `c`), so nothing matches and nothing gets
colored.

The fix: for every directory dired loads, also determine that directory's
path relative to the repo root (its "prefix"), then have the classifier
strip that prefix off each reported change before matching, discarding any
reported change that falls outside the prefix entirely (i.e., changes
elsewhere in the repo, unrelated to the directory being rendered). At the
repo root the prefix is empty and this is a no-op, so root-level coloring is
unaffected.

This is a targeted fix to the matching/scoping logic introduced in PRDs
017-021. It does not change the five-tier color legend, priority ordering,
exact-vs-descendant match distinction, or git-invocation flags from PRD 021
— all of that continues to operate exactly as shipped, just on correctly
scoped input.

## User Stories

1. As a user, I want a modified file inside a subdirectory I've navigated
   into to render yellow, exactly as it would if I were viewing it from the
   repo root, so that git coloring doesn't silently stop working as soon as
   I go deeper than one level.
2. As a user, I want a new (untracked) file inside a subdirectory to render
   its containing directories yellow (edited) at every level between it and
   wherever I'm currently browsing, so that "something changed below here"
   is visible no matter how deep I've navigated.
3. As a user, I want a deleted file inside a subdirectory to render red at
   every level between it and wherever I'm currently browsing, so removals
   stay visible no matter how deep I am in the tree.
4. As a user, I want a conflicted file inside a subdirectory to render
   purple at every level between it and wherever I'm currently browsing, so
   conflicts stay visible no matter how deep I am in the tree.
5. As a user, I want this to work correctly no matter how many levels deep
   I've navigated (one level, two levels, arbitrarily many), not just at the
   repo root or exactly one level below it.
6. As a user, I want the repo root's own coloring to be completely
   unaffected by this fix — it already works correctly today.
7. As a user, I want a change that exists elsewhere in the repository, in a
   part of the tree unrelated to the directory I'm currently viewing, to
   never affect the coloring of what I'm looking at — even if a folder or
   file there happens to share a name with something in my current
   directory.
8. As a developer, I want the "which directory am I in, relative to the repo
   root" computation to be delegated to git itself rather than reimplemented
   in dired's own code, so that edge cases git already handles correctly
   (symlinks, unusual repo layouts) aren't a source of new bugs.
9. As a developer, I want the prefix-stripping/filtering logic to live in the
   `gitstatus` module rather than `loaddir`, so it stays covered by
   `gitstatus`'s existing table-driven unit tests instead of falling into
   the manual-verification-only bucket that `loaddir`'s git-invocation code
   already sits in.

## Implementation Decisions

- **`loaddir` module (modified) — a second, small git invocation per
  directory load.** Alongside the existing `git -C <path> status
  --porcelain --ignored` call, `loaddir` also runs `git -C <path> rev-parse
  --show-prefix`, scoped to the same directory, to obtain that directory's
  path relative to the repo's top-level directory (e.g. `b/c/`, or an empty
  string when `<path>` is itself the repo root). This mirrors the existing
  precedent (PRD 018/021) of `loaddir` performing synchronous git I/O per
  directory load; it is now two small synchronous calls instead of one, not
  a new asynchronous or batched mechanism.
- **`gitstatus` module (modified) — `classify_git_status()` gains a new
  parameter carrying this prefix.** Before matching a porcelain-reported
  path against any entry, the classifier strips the prefix off the start of
  that path. Any porcelain line whose path does not start with the given
  prefix is dropped before matching begins — it describes a change outside
  the directory currently being rendered and must not be considered, even
  if by coincidence it would otherwise text-match an entry in the current
  directory. An empty prefix strips nothing and matches today's shipped
  behavior exactly (no special-casing required — an empty-string prefix is
  a no-op under the same strip/match logic used for a non-empty one).
- **No other module changes.** The five-tier tag set, priority ordering,
  exact-vs-descendant match distinction, and `style_colors()` color mapping
  introduced in PRD 021 are unchanged — this fix only changes what input
  reaches the existing matching logic, not the logic itself.
- These two decisions (git-native prefix lookup; filtering logic placed in
  `gitstatus`, not `loaddir`) were confirmed directly with the user via
  `/grill-me` interview, including a concrete false-positive scenario
  (two same-named subfolders at different points in the repo tree) used to
  confirm that out-of-prefix lines must be dropped entirely rather than
  merely deprioritized.

## Testing Decisions

- Good tests here assert on the externally observable result — given this
  porcelain text, this prefix, and this set of entries, what
  `GitStatusTag` does each entry end up with — matching how
  `test/gitstatus_test.c` already tests `classify_git_status()` today
  (table-driven cases: a label, raw porcelain input, entry specs, and
  expected tags).
- `gitstatus` remains the deep module getting the bulk of new coverage.
  Extend `test/gitstatus_test.c`'s existing table-driven cases (now also
  supplying a prefix per case) with: a non-empty prefix where porcelain
  paths are repo-root-relative and only entries whose local name, once the
  prefix is stripped, matches get classified; a porcelain line whose path
  falls outside the given prefix being ignored entirely, including the
  same-name-in-two-places collision case (a change reported outside the
  prefix must not classify an identically-named entry that happens to exist
  inside the prefix); an empty prefix producing identical results to the
  existing pre-fix test cases (regression coverage that root-level behavior
  is unchanged).
- `loaddir`'s new `rev-parse --show-prefix` invocation is impure I/O and,
  matching the existing precedent set in 018/021 for the `git status`
  invocation, is not unit tested — verified manually instead.
- Manual verification: in a real nested git repo, confirm that opening the
  repo root, then a subdirectory, then a further-nested subdirectory, all
  correctly color modified/new/deleted/conflicted files and directories at
  every depth, not just at the root; confirm the repo root's own coloring
  is unchanged from before this fix; confirm a change elsewhere in the repo
  does not bleed into the coloring of an unrelated, currently-viewed
  subdirectory.

## Out of Scope

- Any change to the color legend, tag priority ordering, or
  `style_colors()` mappings established in PRD 021 — those are correct as
  shipped and untouched by this fix.
- Any change to the `-uall`-flag-dropped git invocation semantics from PRD
  021 (how a wholly-untracked directory collapses to a single porcelain
  line versus a tracked directory with untracked contents) — unaffected by
  this fix.
- An async/background/non-blocking variant of either git call in `loaddir`
  (unchanged decision from 018/021).
- Special-casing submodules or descending into a nested repo's own `.git`
  (unchanged decision from 018/021).
- Caching the repo-root prefix across directory loads (e.g. to avoid the
  second git call when navigating within the same repo) — each directory
  load computes it fresh, matching the existing precedent of one
  synchronous git status call per load.

## Further Notes

This is a direct follow-up to PRD 021 (git-status-colors-directory-
semantics-fix, shipped), itself a follow-up to the original "Color entries
by git status" chain (PRDs 017-020, all shipped). It does not revert or
restructure any of that architecture — `gitstatus` is still the single
classifier, `loaddir` still performs synchronous git I/O per directory
load, `style_colors()` in `dired.c` is still the single place mapping a
`StyleTag` to real colors. This PRD only fixes how correctly the classifier
is fed: the directory-vs-repo-root path mismatch that breaks coloring below
the top level.

The root cause was confirmed by direct reproduction (not assumed): running
`git status --porcelain --ignored` from a repo's root, from a subdirectory,
and from a further-nested subdirectory of a throwaway test repo all
produced the identical repo-root-relative path for the same changed file,
demonstrating that `loaddir`'s `-C <path>`-scoped invocation does not change
git's path-reporting behavior — the missing piece is purely the
prefix/offset dired needs to reconcile that root-relative reporting against
the directory-relative entries it's trying to color.

All decisions in this PRD were reached through direct interview with the
user (via `/grill-me`), not assumed.

---
title: "Git status colors: fix directory semantics and rework the color legend"
description: "Untracked files inside already-tracked subdirectories render with no color at all, and the shipped color legend doesn't match what the user actually wants."
status: needs-triage
---

## Problem Statement

The "color entries by git status" feature (PRDs 017-020, all shipped) does
not behave the way the user needs it to.

Concretely: a new (untracked) file created inside an already-tracked
subdirectory does not cause that subdirectory to render with any git
color at all — it renders identically to a directory with no git changes
in it whatsoever. The user has observed this specifically for untracked
files nested inside tracked subdirectories; whether a bare untracked file
at the top level of a repo has the same problem has not been checked.

Separately, and independently of that symptom, the color legend chunk 4
(020) shipped does not match what the user wants:

- Modified files currently render green; the user wants yellow.
- Untracked files currently render yellow; the user wants green.
- Deleted files currently render the same as any other modification
  (green, soon yellow); the user wants deletions called out in their own
  red color.
- Conflicted files currently render red; the user wants purple, freeing
  up red exclusively for deletions.

There's also a missing concept entirely: today a directory's color is
just "the highest-priority git state found anywhere below it," treating
an untracked child no differently than a modified one. The user
distinguishes two different situations that currently look identical:
a directory git has *never seen before* (wholly new, e.g. just created)
versus a directory git already *knows about* that now merely contains
some new content. Both currently propagate the same way; the user wants
them to render differently.

## Solution

Rework `gitstatus`'s classification and `loaddir`'s git invocation so
that:

1. A brand-new, wholly-untracked directory (nothing inside it is tracked
   by git at any depth) renders the same "new" color as a new file.
2. A directory git already tracks, which merely contains new/changed
   content somewhere below it, renders based on the *worst* kind of
   change found below it — where "worst" now has five tiers instead of
   four, and where new (untracked) content inside an already-tracked
   directory counts as "edited," not "new." Green is reserved
   exclusively for things that are themselves new: a new file, a
   staged-new file, or a directory git has never seen before.
3. The color legend becomes: untracked/staged-new = green,
   modified/edited = yellow, deleted = red (new, previously deletions
   were indistinguishable from any other modification), conflicted =
   purple (previously red), ignored = dim gray (unchanged), clean =
   default (unchanged).

The mechanism that makes directories-vs-their-contents distinguishable
at all is a change to which `git status` mode `loaddir` asks for: by
dropping the `-uall` flag it currently passes, git's own default
untracked-file reporting already collapses a wholly-untracked directory
into a single porcelain line for that directory, while still expanding
untracked files individually inside directories git already tracks. The
classifier can then tell "this porcelain line describes the directory
entry itself" apart from "this porcelain line describes something
nested inside the directory entry" and treat the two cases differently.

## User Stories

1. As a user, I want a new file to render green, so that I can spot
   things I haven't added to git yet.
2. As a user, I want a file staged for addition (`git add`ed but not yet
   committed) to render the same green as an untracked file, so that
   staging a new file doesn't change how I perceive it as "new."
3. As a user, I want a file with unstaged or staged edits to render
   yellow, so that I can spot what I've changed without it being
   confused with a brand-new file.
4. As a user, I want a deleted file to render red, so that removals are
   visually distinct from ordinary edits and from new files.
5. As a user, I want a file with a merge conflict to render purple, so
   that conflicts stand out from an ordinary deletion or edit.
6. As a user, I want an ignored file to keep rendering in its existing
   dim/muted color, so that this fix doesn't disturb a state that
   already works correctly.
7. As a user, I want a directory that git has never seen before (e.g. I
   just created it and haven't `git add`ed anything inside it) to render
   green, the same as a new file, so that "this whole thing is new" is
   obvious without opening it.
8. As a user, I want a directory that git already tracks, which now
   contains a new file I haven't added yet, to render yellow ("edited"),
   not green, so that I can tell "something in here changed" apart from
   "this whole directory is new."
9. As a user, I want a directory containing a deleted file anywhere below
   it to render red, so that I don't have to open every subdirectory to
   discover something was removed.
10. As a user, I want a directory containing a conflicted file anywhere
    below it to render purple, so that I can find conflicts without
    manually walking the tree.
11. As a user, when a directory contains more than one kind of change
    below it (e.g. one deleted file and one merely-edited file), I want
    it to render using the single most attention-grabbing color among
    them, in the order conflicted > deleted > edited > new > ignored, so
    that the directory's color always reflects the thing most worth my
    attention.
12. As a user, I want a clean file or directory to keep rendering exactly
    as it does today (no color), so that the common case stays visually
    quiet.
13. As a user, I want a file or directory outside of any git repository
    to keep rendering exactly as it does today, so that non-repo
    browsing isn't cluttered with git-related color.
14. As a user, I want a selected git-colored row to still show which git
    state it's in, with "selected" still clearly readable, exactly as
    the existing selected-row treatment already does for the other git
    colors.
15. As a developer, I want the existing single-porcelain-parse module
    (`gitstatus`) to remain the one place that turns raw `git status`
    output into per-entry classification, so this fix slots into the
    existing architecture instead of introducing a second classifier.
16. As a developer, I want the "is this the directory's own status, or a
    descendant's" distinction to be a property of how a porcelain path is
    matched against an entry, not a special case bolted onto the
    render/color layer, so that `view.c` and `dired.c` keep working
    purely off of `Entry.git_status` as they do today.

## Implementation Decisions

- **`gitstatus` module (modified) — `GitStatusTag` gains a `DELETED`
  tier**, inserted into the existing priority ordering so it now reads
  `NONE < IGNORED < UNTRACKED < MODIFIED < DELETED < CONFLICTED`. The
  numeric ordering is still what the classifier uses to pick the "worst"
  status when more than one applies to the same entry (unchanged
  mechanism, one more tier).
- **`gitstatus` module (modified) — `tag_for_code(x, y)` gains two new
  cases**, evaluated in this order: existing conflict checks first (`U`
  in either column, `AA`, `DD` — unchanged, so `DD` is still a conflict,
  not a deletion); then a check for staged-new (`x=='A' && y==' '`
  exactly) mapping to `UNTRACKED`, same tier as `??`; then a check for
  `x=='D' || y=='D'` mapping to `DELETED`; the existing catch-all
  ("anything else with a non-space code") remains last and still maps to
  `MODIFIED`. This means renames, `AM`, and any other combination not
  explicitly called out above keep falling into `MODIFIED`, matching
  today's behavior for those cases.
- **`gitstatus` module (modified) — matching a porcelain path against a
  directory entry now distinguishes two cases** instead of treating them
  identically:
  - *Exact match*: the porcelain path equals the directory entry's own
    name (optionally with a trailing slash). This is the directory's own
    reported status and is applied to the entry directly, un-remapped —
    this is how a wholly-untracked directory ends up `UNTRACKED` (green)
    on its own row.
  - *Descendant match*: the porcelain path starts with the directory
    entry's name followed by `/` and has more path content after that.
    This describes something changed *inside* the directory, not the
    directory's own status. The reported tag is applied to the entry
    with one remap: `UNTRACKED` becomes `MODIFIED` before the
    highest-priority comparison runs. Every other tag (`IGNORED`,
    `MODIFIED`, `DELETED`, `CONFLICTED`) propagates unchanged. This is
    the mechanism behind "a tracked directory with a new file inside it
    renders edited/yellow, not new/green."
  - File entries are unaffected by this split — a porcelain path can only
    ever exactly-match a file entry (files have no descendants), so
    their classification is unchanged.
  - Because dropping `-uall` (see below) means a wholly-untracked
    directory is reported by git as a single collapsed porcelain line
    for that directory, a directory entry only ever receives an exact
    match *or* descendant matches, never both — no ordering/precedence
    question arises between the two cases for a single directory.
- **`loaddir` module (modified) — drop the `-uall` flag** from the
  `git status --porcelain --ignored -uall` invocation, using git's
  default `--untracked-files=normal` mode instead. This is what makes
  "directory is itself wholly untracked" observable as a single
  porcelain line (`?? dirname/`) versus "directory is tracked but has an
  untracked file inside" (`?? dirname/file.txt`, listed individually,
  however deeply nested). No other change to how `loaddir` invokes or
  parses git status output.
- **`view`/`dired` modules (modified) — `StyleTag` gains a
  deleted/deleted-selected pair**, following the existing pattern used
  for conflicted/modified/untracked/ignored (an unselected variant using
  the git color as foreground on default background, and a selected
  variant swapping the git color to background with a contrasting black
  or white foreground — the same convention `style_colors()` in
  `dired.c` already uses for every other git tag). `style_colors()`'s
  existing color assignments are updated in place: untracked -> green,
  modified -> yellow, conflicted -> purple (termbox2's `TB_MAGENTA`),
  deleted -> red (the color conflicted used to have), ignored ->
  unchanged dim gray. No new architecture is introduced; this is
  entirely new cases/updated constants within the modules the original
  017-020 chain already established as the right places for this logic.

## Testing Decisions

- Good tests here assert on the externally observable result — given
  this porcelain text and this set of entries, what `GitStatusTag` does
  each entry end up with — not on internal call sequencing, matching how
  `test/gitstatus_test.c` already tests `classify_git_status()` today
  (table-driven cases: a label, raw porcelain input, entry specs, and
  expected tags).
- `gitstatus` is the deep module here (small interface — porcelain text
  in, per-entry tags out; all the actual complexity of this fix lives
  behind it) and gets the bulk of new test coverage, extending
  `test/gitstatus_test.c`'s existing table-driven case list with: a
  staged-new file (`A `) classifying as `UNTRACKED`; a deleted file (`D`
  in either column) classifying as `DELETED`; a wholly-untracked
  directory (reported as a single `?? dirname/` line) classifying as
  `UNTRACKED`; a tracked directory with only an untracked file inside it
  (reported as `?? dirname/file.txt`) classifying as `MODIFIED`, not
  `UNTRACKED`; a tracked directory with a mix of an untracked file and a
  deleted file inside it classifying as `DELETED` (the new priority
  ordering); a tracked directory with a wholly-untracked directory
  nested inside it (reported as `?? outer/inner/`, itself tracked at the
  `outer` level) classifying `outer` as `MODIFIED` and, separately if
  `inner` is also an entry in that same listing, `inner` as `UNTRACKED`.
- `loaddir`'s git invocation is impure I/O and, matching the existing
  precedent set in 018 (its `popen`/subprocess plumbing is explicitly
  not unit tested), the `-uall`-flag removal is not unit tested —
  verified manually instead.
- `style_colors()` in `dired.c` remains untested, matching the existing
  precedent set in 020 for `STYLE_SELECTED`/`STYLE_PROMPT`/`STYLE_ERROR`
  and the other git-tag mappings it added.
- Manual verification: in a real repo directory, confirm each of new /
  staged-new / edited / deleted / conflicted / ignored / clean renders
  in its documented color for both files and directories; confirm a
  directory that is itself wholly new (never `git add`ed) renders green;
  confirm a tracked directory containing only a new file renders yellow;
  confirm a non-repo directory renders exactly as before this feature
  existed; confirm selecting a git-colored row keeps it legible and
  still shows its git color.

## Out of Scope

- Renames (`R`) with no other status modifier — continue falling into
  the generic `MODIFIED`/yellow bucket, unchanged from today.
- An async/background/non-blocking variant of the git status call
  (unchanged decision from 018).
- Special-casing submodules or descending into a nested repo's own
  `.git` (unchanged decision from 018).
- A color legend in the help bar or anywhere else in the UI (unchanged
  decision from 020).
- User-configurable color palettes, e.g. respecting `LS_COLORS` or a
  config file (unchanged decision from 020).
- Confirming, via an actual reproduction, why untracked files inside
  tracked subdirectories currently show no color at all. That symptom
  was reported but not diagnosed or reproduced as part of writing this
  PRD; this PRD instead specifies the intended end state (the color
  legend and directory-semantics rework above) directly. The `-uall`
  removal and exact/descendant-match split are expected to resolve the
  reported symptom as a byproduct of fixing the directory semantics, but
  that has not been manually confirmed. If, once implemented, the
  reported symptom persists for some case not covered above, that's a
  sign this PRD's model of the bug was incomplete and needs revisiting.

## Further Notes

This is a direct follow-up to the "Color entries by git status" feature
(PRDs 017-020, all shipped `status: done`). It does not revert any of
that chain's architecture — `gitstatus` is still the single classifier,
`loaddir` still runs one synchronous `git status` call per directory
load, `style_colors()` in `dired.c` is still the single place mapping a
`StyleTag` to real colors — it changes the git invocation flags, extends
the tag set, and reworks the matching/priority logic within that
existing architecture.

All decisions in this PRD were reached through direct interview with the
user (via `/grill-me`), not assumed.

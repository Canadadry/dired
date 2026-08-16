---
title: "Fix glob-mode state consistency bugs (hidden-toggle drop, missing git-status)"
description: "Two related bugs found during PRD033's cross-feature testing leave glob-mode listings in an inconsistent state: toggling hidden files silently drops an active glob, and glob results never show git-status coloring at all."
status: done
---

## Problem Statement

While writing cross-feature integration tests for PRD033
(`docs/prd/033-integration-test-coverage-cross-feature.md`), two related bugs
in glob mode (`g`/`G`) were found and deliberately left unfixed at the time,
per that PRD's boundary against bundling bug fixes into test-writing. Both
are documented in PRD033's Further Notes and are formalized here:

1. **Hidden-toggle drops the active glob.** A user browsing glob results
   (`glob_type != GLOB_NONE`) who presses `a` to toggle hidden files expects
   the glob to stay active with hidden files folded into its narrowed
   results — the same way it already works for a plain filter (`f`/`F`).
   Instead, the listing silently reverts to the full, unfiltered directory
   contents. The model still believes a glob is active
   (`model->glob_type`/`glob_pattern` remain set), so the mode's status
   line/prompt still claims glob mode, but the displayed rows no longer
   reflect it — the narrowing is lost with no error or indication.

2. **Glob results never show git-status coloring.** A user browsing glob
   results inside a git repository expects modified/untracked/staged/
   deleted/ignored files to render with their documented color, the same
   way the plain (non-glob) listing already does. Instead, every glob
   result renders with no git-status color at all, regardless of the file's
   real state, because glob-result entries never go through git-status
   classification in the first place.

Both bugs share a root cause: the two code paths that build/refresh a
listing (the plain-directory reload path, and the glob-build path) were
extended independently over time, and features added to one path
(git-status classification, glob-preserving reload-on-mutation) were not
consistently carried over to the other.

## Solution

Fix both bugs so glob-mode listings are held to the same consistency
guarantees as plain-directory listings: an active glob survives a
hidden-files toggle (re-narrowing the updated entry set, exactly like an
active filter already does), and glob-result entries receive the same
git-status classification pass that plain-directory entries already get.

Add regression coverage for both: a unit test for the hidden-toggle fix
(mirroring the existing `handle_op_succeeded`/`MSG_TOGGLE_HIDDEN` test
style in `test/update_test.c`), and an integration test for the git-status
fix (mirroring `feature_gitstatus_colors.json`'s convention, since
git-status classification runs in the untested `dired.c` execution layer,
not in `update()`).

## User Stories

1. As a dired user, I want an active glob to survive pressing `a`
   (hidden-files toggle), so that toggling hidden files while browsing glob
   results behaves the same way it already does for an active filter,
   instead of silently discarding my glob narrowing.
2. As a dired user, I want dotfiles that match my active glob pattern to
   appear in the glob results when I toggle hidden files on, so that
   hidden-file visibility is honored consistently across every narrowing
   mode (filter and glob alike).
3. As a dired user, I want toggling hidden files off again while a glob is
   active to re-narrow correctly (dropping now-hidden dotfiles from the
   glob results, keeping non-dotfile matches), so that the toggle is
   symmetric in both directions.
4. As a dired user browsing an archive with an active glob, I want the
   hidden-files toggle to preserve that glob the same way the plain
   (non-archive) case does, so that the fix is consistent across both
   browsing contexts — archive-glob results are rebuilt via
   `archive_glob_matches` elsewhere in the codebase (`handle_op_succeeded`),
   so the archive branch of the hidden-toggle handler should follow the
   same pattern.
5. As a dired user browsing glob results in a git repository, I want a
   glob-matched file that is modified/untracked/staged/deleted/ignored to
   render in its documented git-status color, so that glob mode gives me
   the same at-a-glance repository state the plain listing already does.
6. As a dired user, I want glob results outside any git repository (or
   where `git status` fails/is unavailable) to render with no git-status
   color, exactly matching the plain-directory listing's existing
   fallback, so that the fix doesn't introduce a new failure mode for the
   non-repository case.
7. As a maintainer, I want both fixes to reuse the codebase's existing
   correct pattern rather than reinventing one: the archive-glob branch of
   `handle_op_succeeded` already demonstrates "check `glob_type`, rebuild
   glob results" for post-mutation refreshes, and `load_directory` already
   demonstrates "always classify git-status fresh on load" — the fixes
   should extend those same mechanisms to the hidden-toggle and glob-build
   paths respectively, not add a third, divergent implementation.
8. As a maintainer, I want the hidden-toggle-drops-glob fix and the
   glob-missing-git-status fix implemented and tested as two independent
   changes, so that either can land (and be reverted, if needed) without
   the other, since they touch different files and have no dependency
   between them.

## Implementation Decisions

- **Hidden-toggle glob preservation** (`src/update.c`, `MSG_TOGGLE_HIDDEN`
  handler): extend the handler to check `glob_type` before deciding how to
  refresh, mirroring `handle_op_succeeded`'s existing branching exactly:
  - Inside an archive (`archive_depth > 0`): if `glob_type != GLOB_NONE`,
    rebuild via the same `archive_glob_matches` path
    `handle_op_succeeded`'s archive branch already uses; otherwise fall
    back to the existing `populate_entries_from_level` call.
  - Outside an archive: if `glob_type != GLOB_NONE`, issue `CMD_BUILD_GLOB`
    with the current `glob_pattern`/`glob_type` (exactly as
    `handle_op_succeeded` already does for post-mutation refreshes) instead
    of `CMD_LOAD_DIR`; otherwise keep issuing `CMD_LOAD_DIR` as today.
  - Since this exact three-way branch (archive-glob / archive-plain /
    non-archive-glob / non-archive-plain) already exists once in
    `handle_op_succeeded`, extract it into one shared, private helper
    function that both `handle_op_succeeded` and the `MSG_TOGGLE_HIDDEN`
    handler call — a deep module with a single "how do I refresh the
    current listing right now, respecting whatever narrowing is active"
    interface, so a future third caller doesn't have to duplicate this
    branching a third time.
- **Glob git-status classification** (`src/dired.c`,
  `execute_build_glob`/`walk_glob_matches`): after `walk_glob_matches`
  populates the `scratch` entry buffer, run it through the same
  git-status-fetch-and-classify pipeline `load_directory` already uses
  (`src/loaddir.c`: read the porcelain status and prefix for the glob's
  root directory, then classify every scratch entry against it) — glob
  entries already store paths relative to the glob root exactly the way
  `classify_git_status`'s path-matching expects, so no changes to
  `classify_git_status` itself, or to how glob entries store their names,
  are needed.
  - The two helper functions `load_directory` uses to fetch git status
    text (currently private to `src/loaddir.c`) need to become reusable
    from `src/dired.c`; expose them (via `loaddir.h` or a small dedicated
    header) rather than duplicating the `popen`/`git status`/`git
    rev-parse` invocations a second time.
  - This only applies to the non-archive glob path
    (`execute_build_glob`/`walk_glob_matches`); archive members have no
    git-status concept today and are explicitly out of scope (see below).
- Neither fix changes `classify_git_status`'s matching semantics, the
  `Entry` struct's fields, or any user-facing key binding — both are
  internal consistency fixes to existing, already-specified behavior
  (filter's hidden-toggle survival per the existing filter code path;
  git-status coloring per the already-documented color mapping in
  `feature_gitstatus_colors.json`/PRD-level color specs).

## Implementation Chunks

1. Hidden-toggle glob preservation (`src/update.c`): extract the shared
   refresh helper from `handle_op_succeeded`, wire `MSG_TOGGLE_HIDDEN` to
   use it, add unit test coverage. Independent of chunk 2.
2. Glob-result git-status classification (`src/loaddir.c` /
   `src/dired.c`): expose the git-status-fetch helpers, wire
   `execute_build_glob` to classify `scratch` before returning, add
   integration test coverage. Independent of chunk 1.

## Testing Decisions

- **Chunk 1** is a pure `Model`/`Cmd` state-transition change reachable
  from `update()`, so it gets unit tests in `test/update_test.c`, following
  the existing `test_toggle_hidden`/
  `test_toggle_hidden_inside_archive_repopulates_entries_without_cmd`/
  `test_op_succeeded_rebuilds_glob_when_active` tests as direct prior art:
  construct a `Model` with `glob_type`/`glob_pattern` set, send
  `MSG_TOGGLE_HIDDEN`, assert `cmd.type == CMD_BUILD_GLOB` with the
  preserved pattern (non-archive case) or that entries are correctly
  rebuilt via the archive-glob path with `cmd.type == CMD_NONE` (archive
  case) — mirroring the assertion style of the tests above, not inspecting
  rendered output.
- **Chunk 2** touches `src/dired.c`'s `execute_build_glob`, which (like the
  rest of `dired.c`'s `execute_*` functions) has no dedicated unit test
  file and is only exercised end-to-end via the pty integration harness.
  Add an integration test extending `test/integration/`'s existing
  conventions: fixture with a git `setup` (`init`/`add`/`commit`) plus a
  `mutate` to produce a modified tracked file, matching
  `feature_gitstatus_colors.json`'s and
  `cross_gitstatus_refresh_after_delete.json`'s fixture pattern; enter glob
  mode, commit a pattern matching the modified file, and assert its row
  renders with the correct git-status color — where
  `feature_gitstatus_colors.json` already established the color-to-state
  mapping for the plain listing, this test only needs to prove the same
  mapping now also applies inside glob results.
- Also extend the already-committed but scope-narrowed
  `test/integration/cross_hidden_toggle_with_filter.json` scenario (or add
  a sibling `cross_hidden_toggle_with_glob.json`) once chunk 1 lands,
  covering the glob half of PRD033's User Story 4 that was deliberately
  left out — the fixture/step shape can mirror the filter version almost
  directly, swapping `f`/pattern/Enter for `g`/pattern/Enter.
- Good tests here match this codebase's established split: `update()`
  logic is unit-tested on `Cmd`/`Model` output, `dired.c` execution-layer
  logic (real git status, real directory walks) is integration-tested on
  rendered screen output — never on internal state.

## Out of Scope

- Archive-member git-status classification — archives have no git-status
  concept anywhere in the current codebase (member entries are never
  git-classified even outside glob mode), and introducing one is a
  separate, larger feature, not part of this bug fix.
- Any other narrowing-mode combination not already covered by PRD032/033's
  existing tests (e.g. this PRD does not re-audit filter mode's
  hidden-toggle behavior, which is already confirmed correct and tested).
- Performance of the `git status` shell-out being run an additional time
  for glob builds — this PRD reuses the existing (already-accepted)
  shell-out-per-load approach `load_directory` already uses; optimizing
  that mechanism is out of scope.

## Further Notes

- Both bugs were originally discovered and documented (with full source
  citations) in `docs/prd/033-integration-test-coverage-cross-feature.md`'s
  Further Notes section, under the "Investigation for User Story 4" and
  "Investigation for User Story 7" bullets — that PRD's own scope
  explicitly excluded fixing them ("fixing any bug a new test uncovers is
  a separate follow-up, not bundled into writing the test itself"). This
  PRD is that follow-up.
- Once this PRD lands, the two integration tests it were deliberately
  scoped away from in PRD032/033
  (`cross_hidden_toggle_with_filter.json`'s missing glob half, and
  `cross_gitstatus_refresh_after_delete.json`'s missing glob-coverage half)
  become addressable and should be added as part of this PRD's own testing
  work, per the Testing Decisions above, rather than deferred again.

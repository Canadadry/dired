---
title: "Integration test coverage: one test per remaining feature"
description: "The tty integration harness (PRD 030) covers only a handful of scenarios so far; most of dired's internal-behavior feature surface has no end-to-end screen-output coverage yet."
status: done
---

## Problem Statement

The pty-driven integration harness (`docs/prd/030-tty-integration-test-harness.md`)
landed with `test/integration/run.py` and eight `*.json` test files: basic
navigation/rename, copy/paste, move/paste, create file/dir, plain-substring
filter, multiselect batch delete, and two regression scenarios (PRD021,
PRD024) for git-status coloring edge cases. That covers a small fraction of
the features listed in `Readme.md` and specified across `docs/prd/`. Most of
dired's own internal behavior (excluding anything that launches an
interactive external process — `vim`, `less`/`hexdump` preview, actually
*running* a shell command) has zero end-to-end screen-output coverage:
regex filter, glob mode, sort/group, hidden-files toggle, pagination, most
of multi-select, single-entry delete (trash and permanent), command-history
recall, the in-app help page, and the baseline git-status color mapping.

## Solution

Add one `test/integration/feature_<name>.json` file per feature below,
following the existing test file format (`fixture` + ordered `steps`, each
with `keys` and optional tagged-screen `expect`) and the existing
`smoke_*`/`prd0NN_*` files as prior art. Two features (trash-delete,
command-history recall) depend on the debug-build `HOME`-resolves-to-cwd
change (a separate PRD) landing first, since without it those tests would
read/write the real developer's `$HOME`.

Archive browsing is explicitly excluded from this pass (see Out of Scope) —
there's currently no way to materialize a real `.zip`/`.tar.gz` fixture
through the declarative fixture builder, and that question is deferred to a
future PRD about dired gaining its own archive-creation commands.

## User Stories

1. As a maintainer, I want a regex-filter (`F`) integration test, so that
   the regex-mode narrowing behavior is covered the same way plain-filter
   (`f`) already is.
2. As a maintainer, I want plain-glob (`g`) and regex-glob (`G`) integration
   tests, so that glob's recursive-walk listing (PRD013) is verified against
   real rendered output, not just unit-level.
3. As a maintainer, I want a sort-cycling (`s`) integration test, so that
   cycling through name/date/size/extension order is verified against the
   actual rendered listing order.
4. As a maintainer, I want a group-toggle (`d`) integration test, so that
   directories-first grouping is verified against actual rendered order.
5. As a maintainer, I want a hidden-files-toggle (`a`) integration test, so
   that dotfiles appearing/disappearing from the listing is verified
   end-to-end.
6. As a maintainer, I want a pagination (`o`, "Next page") integration test,
   so that paging through a directory with more entries than fit on screen
   is verified against real rendered output.
7. As a maintainer, I want integration tests for the multi-select behaviors
   not yet covered — range-select (`r`), select-all/none (`t`), batch copy,
   batch move, marked-row visuals (blue / reverse-video per PRD027), and the
   mode title/help text (PRD028) — so that multi-select's full surface has
   the same end-to-end coverage batch-delete already has.
8. As a maintainer, I want a single-entry trash-delete (`Backspace`)
   integration test and a separate permanent-delete (`x`, with its
   confirmation prompt) integration test, so that both deletion paths are
   verified against real rendered output, not just `test/trash_test.c`'s
   unit-level coverage.
9. As a maintainer, I want a command-history-recall integration test that
   opens the `:!` prompt and browses history with `<Up>`/`<Down>` *without*
   pressing Enter, so that recall UI is covered without ever forking a real
   shell command (which stays out of harness scope).
10. As a maintainer, I want an in-app help page (`h`) integration test, so
    that opening and dismissing the help overlay is verified against real
    rendered output.
11. As a maintainer, I want a git-status color mapping integration test
    covering the baseline happy path (modified, untracked, staged, deleted,
    ignored each rendering their documented color), so that coverage isn't
    limited to the two specific edge-case regressions (PRD021, PRD024) that
    already have tests.
12. As a maintainer, I want a yank-cancel (`Esc`) integration test for a
    pending copy/move, so that cancelling a yank is verified against real
    rendered output (the listing/status line reflecting no pending
    operation), not just implied by the existing copy/move-through-to-paste
    tests.
13. As a maintainer, I want each new test file named `feature_<name>.json`,
    consistent with the existing `smoke_*`/`prd0NN_*` naming, so that this
    batch is easy to distinguish from prior work and from the separate
    cross-feature-interaction test PRD.

## Implementation Decisions

- Each feature gets its own `test/integration/feature_<name>.json` file,
  one file per user story above (13 files, e.g. `feature_regex_filter.json`,
  `feature_glob_plain.json`, `feature_glob_regex.json`, `feature_sort.json`,
  `feature_group_toggle.json`, `feature_hidden_toggle.json`,
  `feature_pagination.json`, `feature_multiselect_range.json`,
  `feature_multiselect_select_all.json`, `feature_multiselect_batch_copy.json`,
  `feature_multiselect_batch_move.json`, `feature_multiselect_visuals.json`,
  `feature_trash_delete.json`, `feature_permanent_delete.json`,
  `feature_history_recall.json`, `feature_help_page.json`,
  `feature_gitstatus_colors.json`, `feature_yank_cancel.json` — exact count
  may split or merge where one JSON file can cover a feature's happy path
  and its immediate variants without becoming unwieldy, e.g. multi-select
  visuals may fold into the range-select or select-all file rather than
  standing alone).
- No changes to `run.py`, `fixture.py`, `ptysession.py`, `keynotation.py`,
  or `taggedscreen.py` — this PRD is pure test-content, reusing the existing
  harness exactly as-is.
- `feature_trash_delete.json` and `feature_history_recall.json` depend on
  the debug-build `HOME`-resolves-to-cwd PRD landing first; sequence this
  PRD's implementation after that one (or land every other feature file
  first and these two last).
- `feature_history_recall.json`'s fixture pre-seeds a starting history file
  by including `.config/dired_history`'s expected on-disk contents directly
  in the fixture's `tree` (once `HOME` resolves to the fixture root in debug
  builds, this path lands inside the fixture like any other file) — no new
  fixture-builder capability needed, since the existing `tree` spec already
  supports arbitrary relative paths.
- `feature_gitstatus_colors.json` reuses the same git `setup`
  (`add`/`commit`) fixture whitelist the existing PRD021/024 tests already
  use; no new fixture capability needed.

## Testing Decisions

- Every test in this PRD is a `test/integration/*.json` file run by the
  existing `run.py`, asserting full tagged-screen rows exactly as
  `smoke_*`/`prd0NN_*` files already do — no partial-row or
  substring-matching mechanism is introduced.
- Prior art: `smoke_navigation_rename.json` (multi-step keys+expect
  sequence), `prd021_untracked_in_tracked_subdir.json` /
  `prd024_modified_below_repo_root.json` (git fixture `setup` usage,
  git-status color tag assertions).
- Good tests here assert only on rendered screen output (or absence of a
  change, e.g. cancelled yank), never on internal `Model` state — matching
  `030-tty-integration-test-harness.md`'s existing testing philosophy.

## Out of Scope

- Archive browsing (`tar`/`unzip` listing navigation) — blocked on a
  fixture mechanism for real archive files; deferred until the future PRD
  about dired gaining `:zip`/`:tar.gz` archive-creation commands lands and
  is grilled separately, since it may resolve the fixture question for
  free.
- Anything that launches an interactive external process attached to the
  pty: file preview (`space`, always pipes through `less`/`hexdump`),
  per-extension preview commands, editing a file in `vim`, and *executing*
  a run-shell-command (only its history-recall UI, which never forks, is in
  scope).
- Cross-feature interaction scenarios (two independently-specified features
  combined) — covered by a separate PRD.
- Any harness/runner changes — this PRD is test-content only.

## Further Notes

- This PRD's `feature_trash_delete.json` and `feature_history_recall.json`
  stories are the concrete motivation for the debug-build `HOME`-resolves-
  to-cwd PRD; if that PRD is deprioritized, every other feature file here
  can still land independently.
- `feature_gitstatus_colors.json` covers 4 of the 5 states from User Story
  11 (modified, untracked, staged, ignored); "deleted" is blocked on a
  fixture-harness capability (`git rm` / file-removal mutation) that this
  PRD's fixture whitelist and `mutate` mechanism don't support, and is
  deferred to a future PRD that extends the harness and this test.
- `feature_history_recall.json` covers only the no-history-yet Up/Down
  no-op case; recall-of-a-real-entry is blocked because `tree`/`mutate`
  fixture content is written literally with no runtime-path templating,
  while the history file's on-disk key must equal the fixture root's exact
  `getcwd()` value at test time — deferred to a future harness PRD that
  adds `{fixture_root}` substitution to `fixture.py`'s tree/mutate writers.
- User Story 10 (`feature_help_page.json`) is not implemented because
  `execute_help()` is structurally identical to the already-excluded
  `space`-preview path (same `execute_preview()` call, `less -R` forked
  directly onto the pty, dired's own renderer offline for the duration) —
  it falls under the PRD's existing Out of Scope exclusion for
  interactive-external-process features, even though `h` isn't named
  explicitly there.

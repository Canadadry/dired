---
title: "Integration test coverage: cross-feature interaction bugs"
description: "Individual features each have their own tests, but nothing exercises the undocumented behavior where two independently-specified features are both active at once."
status: needs-triage
---

## Problem Statement

Each of dired's features is specified and tested (unit-level, and after a
companion PRD, integration-level) in isolation. But dired's own PRDs already
flag several places where two features interact and the interaction itself
is the specified behavior — e.g. PRD025 says sort/group/filter/glob keys
must no-op while multi-select marks exist, and PRD027 says a mark visually
overrides git-status coloring on the same row. Those interaction rules are
easy to get right initially and easy to silently break later, because a
regression in one feature's own tests would never catch a break in how it
behaves *while another feature is also active* — no single feature's test
suite exercises that combination. Other interactions (pagination position
after a sort/group change, a filter surviving a rename, git-status coloring
refreshing correctly after a delete) aren't formally specified anywhere but
are exactly the kind of thing a real user hits by accident.

## Solution

Add a `test/integration/cross_<scenario>.json` test per scenario below,
using the same harness and file format as the per-feature test PRD, but
each one deliberately combines two independently-specified features to
verify their documented (or reasonably-expected) interaction rather than
either feature's behavior alone.

## User Stories

1. As a maintainer, I want a test that marks entries, then presses each of
   sort (`s`), group (`d`), filter (`f`), and glob (`g`), so that PRD025's
   "these keys no-op while marks exist" rule is verified against real
   rendered output (both the listing order/content and the marks
   themselves staying unchanged), not just implied by unit tests of
   `update()`.
2. As a maintainer, I want a test that marks a git-tracked, modified file
   and asserts its rendered row shows the marked style (blue /
   reverse-video), not the git-status modified color, so that PRD027's
   documented precedence rule is verified against actual screen output —
   today it's only asserted at the `view()` return-value level.
3. As a maintainer, I want a test that attempts to enter selection mode
   (`v`) while glob results are being browsed (`MODE_GLOB`), so that
   PRD025's explicit rejection (with its error message) is verified
   end-to-end.
4. As a maintainer, I want a test that toggles hidden files (`a`) while a
   filter or glob is active, so that dotfiles correctly appear in/disappear
   from the already-narrowed listing rather than the toggle being silently
   ignored or the filter being dropped.
5. As a maintainer, I want a test that changes sort or group while on a
   page other than the first (pagination active), so that the visible page
   resets or re-anchors sensibly instead of showing a stale/out-of-range
   offset into the reordered listing.
6. As a maintainer, I want a test that renames an entry while a filter or
   glob is active, so that I can see whether the renamed entry stays
   correctly visible/positioned under the still-active narrowing, or
   whether the operation leaves the listing in an inconsistent state.
7. As a maintainer, I want a test that trashes or deletes a git-tracked
   file, so that I can see whether the remaining listing's git-status
   colors refresh correctly rather than showing stale coloring from before
   the deletion.
8. As a maintainer, I want a test that starts a copy or move yank, then
   navigates to a different directory before pasting, so that I can see
   whether the pending yank survives navigation as intended or is
   unexpectedly dropped/still applies to the wrong source.
9. As a maintainer, I want a test that starts a copy or move yank and then
   presses `Esc`, followed by an attempted paste, so that I can confirm a
   cancelled yank leaves nothing to paste (paste is a no-op or shows no
   pending operation), verified end-to-end rather than only via the
   single-feature yank-cancel test's immediate-cancellation assertion.

## Implementation Decisions

- Each test lives in its own `test/integration/cross_<scenario>.json`,
  e.g. `cross_marks_reject_sort_group_filter_glob.json`,
  `cross_marks_override_gitstatus_color.json`,
  `cross_select_mode_rejected_in_glob.json`,
  `cross_hidden_toggle_with_filter.json`,
  `cross_pagination_after_sort_change.json`,
  `cross_rename_under_active_filter.json`,
  `cross_gitstatus_refresh_after_delete.json`,
  `cross_yank_survives_navigation.json`,
  `cross_yank_cancel_then_paste.json`.
- No harness/runner changes — reuses `run.py`, `fixture.py`, and the
  existing JSON test format exactly as-is, same as the per-feature PRD.
- Where a scenario's expected behavior is already explicitly specified by
  an existing PRD (stories 1–3 above: PRD025, PRD027), the test asserts
  exactly that documented behavior. Where no PRD specifies the expected
  outcome (stories 4–9), the test asserts whatever behavior a first
  implementation run reveals as correct *only after confirming with the
  maintainer that the observed behavior is in fact the intended one* — a
  cross-feature test whose author silently codifies undesired behavior as
  "expected" would defeat this PRD's purpose of finding bugs, not
  papering over them.
- Boundary rule for what belongs in this PRD vs. the per-feature PRD: a
  scenario belongs here only if it combines two *independently*-specified
  features where no single feature's own PRD says what should happen when
  both are active — a feature's own directly-specified sub-behaviors (e.g.
  multi-select's own batch actions) belong to the per-feature PRD instead.

## Testing Decisions

- Same conventions as the per-feature test PRD: full tagged-screen row
  assertions via the existing harness, no internal-state inspection.
- Because several of these scenarios (stories 4–9) don't have a
  pre-specified correct answer, running each new test for the first time
  is itself part of the verification step — a failing assertion here may
  mean "found a real bug" rather than "wrote the wrong expected output,"
  and that distinction should be resolved with the maintainer before
  hardcoding the observed screen as the expected one.
- Prior art: same as the per-feature PRD (`prd021_...json`/
  `prd024_...json` for git-status assertions,
  `smoke_multiselect_batch_delete.json` for multi-select mechanics).

## Out of Scope

- Archive browsing combined with any other feature — deferred alongside the
  per-feature PRD's archive exclusion, pending the future archive-commands
  PRD.
- Any interaction involving an external interactive process (preview,
  `vim`, actually running a shell command) — same harness limitation as the
  per-feature PRD.
- Any harness/runner changes.
- Discovering and fixing bugs is explicitly in scope for this PRD's intent,
  but *fixing* any bug a new test uncovers is a separate follow-up, not
  bundled into writing the test itself.

## Further Notes

- This PRD assumes the per-feature integration test PRD lands first (or at
  least in parallel), since several scenarios here build directly on
  fixtures/steps similar to those single-feature tests (e.g. the marks
  scenarios need multi-select's basic mark/unmark mechanics working and
  tested already).
- A fourth, deferred PRD (dired gaining `:zip`/`:tar.gz` archive-creation
  and extraction commands, to be grilled in its own dedicated session) may
  eventually unblock archive-related cross-feature scenarios too.
- Investigation for User Story 4 found that the glob half of this scenario
  doesn't reproduce the filter half's correct behavior: `MSG_TOGGLE_HIDDEN`
  (`src/update.c`) never inspects `glob_type` and unconditionally reloads
  via `CMD_LOAD_DIR`, so `handle_dir_loaded` reapplies `filter_type` —
  which entering glob mode has already forced to `FILTER_NONE` — silently
  dropping the glob's narrowing and reverting to the plain directory
  listing, even though `model->glob_type`/`glob_pattern` remain set
  internally. This is a real bug, not a design gap: `handle_op_succeeded`
  already contains the correct pattern (checking `glob_type` and reissuing
  `CMD_BUILD_GLOB` after rename/delete/paste refreshes) that
  `MSG_TOGGLE_HIDDEN` simply doesn't follow.
  `cross_hidden_toggle_with_filter.json` therefore covers only the
  filter+hidden-toggle combination (confirmed correct); fixing the
  glob+hidden-toggle bug and adding its regression test is deferred to a
  follow-up, per this PRD's Out of Scope boundary on not bundling bug
  fixes into test-writing.

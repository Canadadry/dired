---
title: "Debug builds resolve HOME to cwd for trash/history/config"
description: "Trash, command history, and preview config all hardcode $HOME in debug builds too, so integration/manual debug runs read and write the real developer's home directory instead of an isolated location."
status: done
---

## Problem Statement

`diredd` (the debug binary, built with `-DBUILD_DEBUG`) is used both by a
human running `./builder debug clean dired` for manual testing and by the
`test/integration/` pty harness (`test/integration/ptysession.py`), which
spawns `diredd` with a fresh temp directory as its `cwd`. Three places in
the code resolve `getenv("HOME")` directly and write real files there
regardless of which of those two contexts is running:

- `ensure_trash_dir` (`src/trash.c`) moves deleted entries to `$HOME/.trash`.
- `history_default_path` (`src/history.c`) loads/saves command history at
  `$HOME/.config/dired_history`.
- `load_preview_config` (`src/dired.c`) loads (and, on first run, creates)
  the per-extension preview config at `$HOME/.config/dired`.

Because the pty harness runs `diredd` with `env = dict(os.environ)`
(inheriting the real `$HOME`), any integration test that exercises trash or
history would move real files into the developer's actual `~/.trash` or
read/write their actual `~/.config/dired_history` — nondeterministic across
machines and destructive to real state. This currently blocks writing any
integration test for the trash-delete or command-history-recall features.

## Solution

In debug builds only, resolve the effective "home" for these three paths to
the process's current working directory instead of `$HOME`, via one shared
helper used by all three call sites. Since dired's own process never
`chdir()`s during normal operation (the only `chdir()` in the codebase is
inside a forked child for run-shell-command execution) and the pty harness
already launches `diredd` with each test's isolated fixture directory as its
`cwd`, this gives every integration test an isolated `.trash`,
`.config/dired_history`, and `.config/dired` for free, with no changes
needed to the Python harness. Release builds (`dired`) are unaffected.

## User Stories

1. As a maintainer writing an integration test that deletes a file, I want
   the trashed file to land under the test's own fixture directory, so that
   running the test suite never touches my real `~/.trash`.
2. As a maintainer writing an integration test that recalls command history,
   I want history to load from and save to a location scoped to that test's
   fixture, so that the test's behavior doesn't depend on whatever is
   already in my real `~/.config/dired_history`, and doesn't corrupt it.
3. As a maintainer running `./builder debug clean dired` interactively to
   manually test a change, I want the same cwd-scoped behavior to apply, so
   that debug-build behavior is consistent regardless of who's driving it or
   why (test harness vs. a human), and my real `~/.trash`/history/config
   stay untouched by debug-build experimentation too.
4. As a maintainer, I want the release binary (`dired`) to be completely
   unaffected by this change, so that real users' trash, history, and
   preview config continue to live at the standard `$HOME`-relative
   locations they already do today.
5. As a maintainer, I want a single shared helper implementing the
   cwd-vs-`$HOME` decision, rather than three independent `#ifdef
   BUILD_DEBUG` branches, so that the behavior is defined and can change in
   exactly one place.
6. As a maintainer, I want this to be unconditional in debug builds (no env
   var, no opt-in flag), so that the behavior is simple and predictable
   rather than one more thing to remember to set.
7. As a maintainer of `test/trash_test.c` (which currently isolates each
   test case via `setenv("HOME", ...)` before calling `trash_item`), I want
   those tests updated to isolate via the process's `cwd` instead, so that
   they keep passing and keep asserting real isolation once the resolver
   stops consulting `$HOME` in debug builds.

## Implementation Decisions

- Add one shared helper, e.g. `dired_effective_home(char *out, size_t
  out_size)` (return `0`/`-1` like `history_default_path` and
  `ensure_trash_dir` already do for their own lookups), likely placed in
  `helpers.c`/`helpers.h` alongside the other small path/string utilities
  already there.
  - In debug builds (`#ifdef BUILD_DEBUG`): fills `out` with `getcwd()`'s
    result (bounded to `out_size`); returns `-1` on `getcwd()` failure.
  - In release builds: fills `out` with `getenv("HOME")`, exactly matching
    each site's current empty/missing check; returns `-1` if unset/empty,
    exactly as today.
- Update all three call sites to call this helper instead of `getenv("HOME")`
  directly, keeping each site's own suffix logic unchanged (`/.trash`,
  `/.config/dired_history`, `/.config/dired`).
- No env var gate and no opt-in flag: the substitution is unconditional for
  every `BUILD_DEBUG` binary (`diredd` and the `run_tests` unit-test binary,
  which also links against `libdiredd.a` per `build.c`'s `build_test()`).
- `test/trash_test.c`'s three test cases currently call `setenv("HOME",
  home, 1)` with a fresh `mkdtemp`-created directory before calling
  `trash_item` directly, then read back `$home/.trash`. Once
  `ensure_trash_dir` stops consulting `$HOME` in debug builds, `setenv`
  becomes inert and all three test cases would collide in whatever
  directory the `run_tests` process's cwd happens to be. These tests need
  to switch to isolating via `chdir()` into their own `mkdtemp`-created
  directory instead, asserting against `<that dir>/.trash`, and restoring
  the original cwd afterward (via a saved `getcwd()` at the top of each
  test, restored in all return paths) so `chdir()` state doesn't leak into
  other unrelated test files sharing the same `run_tests` process.
- `test/history_test.c` does not call `history_default_path` (it only
  exercises the pure arena functions), so it needs no changes.

## Testing Decisions

- `dired_effective_home`'s debug-path behavior (returns cwd) is exercised
  indirectly by the updated `test/trash_test.c` cases and by the
  `test/integration/` suite (any test that trashes a file or exercises
  history/config now implicitly asserts isolation); a direct unit test for
  the helper itself, table-driven on debug-vs-release behavior, is
  reasonable to add alongside `test/helpers_test.c` if release-mode behavior
  needs coverage too, but release mode can't be exercised from a
  `BUILD_DEBUG` test binary — cover only the debug branch directly, and
  treat the release branch as a straight passthrough to the existing,
  already-trusted `getenv("HOME")` behavior.
- `test/trash_test.c`'s three existing cases are updated in place (`chdir`
  instead of `setenv("HOME")`), not replaced — same assertions, same
  isolation guarantee, different mechanism.

## Out of Scope

- Any change to release-build (`dired`) path resolution.
- An env var or flag to opt in/out of the cwd substitution in debug builds.
- Changes to the Python integration harness (`test/integration/*.py`) — none
  are needed; `ptysession.py` already launches `diredd` with each fixture's
  root as `cwd`.
- Writing the trash-delete or command-history-recall integration tests
  themselves — this PRD only removes the blocker; those tests are planned
  separately.
- Archive creation/extraction commands (`:zip`, `:tar.gz`) — unrelated,
  deferred to its own future PRD.

## Further Notes

- This PRD is the first of a set: it unblocks two integration tests planned
  in a following per-feature test-coverage PRD (trash-delete,
  command-history recall) that would otherwise pollute a real developer's
  `$HOME`.
- `load_preview_config`'s config file is only consumed by dired's preview
  feature, which itself launches an external pager (`less`/`hexdump`) and
  stays out of scope for the pty integration harness regardless — this PRD
  moves its path for consistency (one shared helper, not two), not because
  an integration test needs it yet.

---
title: "TTY integration test harness"
description: "There is no way to drive dired as a real TUI process and assert on what it renders, so regressions like the git-status coloring bugs (PRDs 021, 024) can only be caught by hand-testing or by unit-testing internals in isolation."
status: done
---

## Problem Statement

dired is a terminal UI built on termbox2 (`vendor/termbox2.h`), and `tb_init()`
hardcodes opening `/dev/tty` directly — it cannot be driven over a plain pipe,
and Claude Code itself cannot interact with it (`never run dired directly you
cannot control a tui app`, per `CLAUDE.md`). The existing test suite
(`test/*.c`, minitest-based) exercises individual modules (`loaddir`,
`gitstatus`, `view`, `update`, ...) in isolation, but nothing exercises dired
end-to-end as a real process: spawned, sent a sequence of keystrokes, and
checked against what actually lands on screen.

This gap has already let real regressions through. PRD 021 shipped a bug
where untracked files nested inside already-tracked subdirectories rendered
with no git color at all. PRD 024 shipped a bug where git-status coloring
stopped working entirely as soon as the user navigated below the repo's
top-level directory, because `git status --porcelain` reports paths relative
to the repo root rather than the directory dired queried. Both were the kind
of bug that a full render — real files on disk, real git state, real
navigation keystrokes, real screen output — would have caught before ship,
but the unit tests only exercise `gitstatus`'s classification logic against
synthetic porcelain strings, not the full pipeline.

## Solution

Build a Python-based integration test harness that spawns dired's debug
binary under a real pseudo-terminal (pty), sends it sequences of keystrokes,
and asserts on the resulting screen contents — including git-status colors —
using self-contained JSON test files, each with its own filesystem/git
fixture.

The harness lives entirely outside the C build system (`build.c`/
`./builder`), in `test/integration/`, with its own `Makefile` and
`requirements.txt`. It talks to dired only through the same channels a real
user does (the pty) plus one small, opt-in synchronization hook compiled only
into debug builds, so it never changes dired's behavior for real users.

v1 scope is dired's own internal behavior — navigation, preview, rename,
git-status coloring, and similar. Launching external processes (the
run-shell-command mode, `$EDITOR`/pager launches) is out of scope for now.

## User Stories

1. As a maintainer, I want to write a JSON file describing a directory/git
   fixture, a sequence of keystrokes, and an expected screen, so that I can
   assert dired's exact rendered output for a scenario without hand-testing
   it in a terminal.
2. As a maintainer, I want each integration test to run against its own
   isolated temp directory and (optionally) its own git repo, so that tests
   never interfere with each other or with dired's own repo.
3. As a maintainer, I want to assert on git-status colors specifically (e.g.
   a modified file inside a subdirectory rendering yellow), so that
   regressions like PRD 021 and PRD 024 are caught automatically instead of
   relying on manual review.
4. As a maintainer, I want to describe expected colored output using simple
   nested tags (e.g. `<yellow>file.txt</yellow>`) rather than raw ANSI codes
   or cell-by-cell attribute tables, so that test files stay readable.
5. As a maintainer, I want to optionally assert on intermediate screen states
   partway through a sequence of keystrokes (checkpoints), so that when a
   long test fails I know which step diverged instead of only seeing the
   final mismatch.
6. As a maintainer, I want checkpoints to be opt-in per step, so that simple
   tests stay terse and only tests that need step-level precision pay for it.
7. As a maintainer, I want dired's own `/dev/tty`-based termbox2
   initialization to work unmodified against the harness, so that the
   harness proves real terminal behavior rather than a mocked one.
8. As a maintainer, I want the harness to know precisely when dired has
   finished rendering after a keystroke, so that tests aren't flaky due to
   fixed sleeps or ambiguous "idle" heuristics racing against slow renders
   (e.g. git status calls, large directory listings).
9. As a maintainer, I want this synchronization mechanism to add zero
   overhead or behavior change to normal (non-debug, non-test) builds of
   dired, so that shipping the feature carries no risk to real users.
10. As a maintainer, I want the synchronization mechanism to be a separate
    channel from dired's actual terminal output, so that the captured screen
    buffer reflects only real rendering, never test-protocol bytes.
11. As a maintainer, I want a fixed, deterministic terminal size for every
    test, so that expected-buffer grids are predictable and don't depend on
    the host terminal's actual dimensions.
12. As a maintainer, I want to describe a test's filesystem fixture
    declaratively (a tree of files/dirs with contents), so that I don't have
    to write imperative setup code per test.
13. As a maintainer, I want to optionally describe git operations for a
    fixture (staging and committing specific files), so that I can construct
    tracked/untracked/modified/deleted/ignored file states without writing
    arbitrary shell code.
14. As a maintainer, I want the set of git operations available to a fixture
    to be a narrow, hardcoded whitelist (`add`, `commit`), so that a test
    file — which may be authored by an automated agent rather than
    hand-reviewed every time — cannot execute arbitrary commands.
15. As a maintainer, I want fixture git commands to never let a test file
    specify a program name at all (only arguments to a hardcoded `git`
    invocation, never passed through a shell), so that command injection via
    shell metacharacters is structurally impossible.
16. As a maintainer, I want `git init` and a fixed local commit identity
    (`user.name`/`user.email`) to be set up automatically whenever a
    fixture's git setup list is non-empty, so that commits never depend on
    (or silently reuse) the host's global git config.
17. As a maintainer, I want fixture git config changes scoped to the
    fixture's own temp repo only, so that running the test suite never
    touches dired's own repository configuration.
18. As a maintainer, I want to express keystroke sequences using a compact,
    readable notation that mixes literal characters and named special keys
    (e.g. `"<Enter>"`, `"<Esc>"`, `"<Down>"`), so that test files reflect
    what a user actually types without needing raw byte arrays.
19. As a maintainer, I want a clear, actionable failure when dired doesn't
    respond within a bounded time (e.g. 5 seconds) after a keystroke, so that
    a hang or crash in dired fails the test loudly instead of hanging the
    whole suite indefinitely.
20. As a maintainer, I want the failure to distinguish "dired crashed/exited"
    from "dired is just slow," so that I can diagnose which one happened
    without re-running under a debugger.
21. As a maintainer, I want test failures to report only the rows that
    differ between expected and actual output (not the entire 80x24 grid),
    so that diagnosing a one-line mismatch doesn't require scanning a wall of
    matching text.
22. As a maintainer, I want the integration test tooling (Python, `pyte`,
    etc.) to be fully separate from the C build (`build.c`/`./builder`), so
    that building/testing dired itself never requires Python, and vice
    versa.
23. As a maintainer, I want a `Makefile` in the integration test directory
    that creates an isolated virtualenv and installs pinned dependencies, so
    that running the suite never pollutes or depends on the system/user
    Python environment.
24. As a maintainer, I want the key-notation encoder, the tagged-screen
    format (encode and parse), and the fixture builder to be testable in
    complete isolation (no pty, no dired process required), so that bugs in
    the harness itself are caught by fast unit tests rather than only by
    flaky end-to-end runs.
25. As a maintainer, I want conflicted git status (`UU`/`AA`) to remain
    covered by the existing `gitstatus` unit tests rather than requiring the
    integration harness to script real merge conflicts, so that the fixture
    git whitelist can stay minimal without losing coverage.

## Implementation Decisions

**Synchronization hook (C side, `src/dired.c`)**
- After the existing `render(&model)` call in `main()`'s loop (the single
  point where rendering happens, immediately before the loop blocks on
  `tb_poll_event`), add a hook that writes one byte to a file descriptor
  taken from the `DIRED_TEST_SYNC_FD` environment variable, if set.
- If `DIRED_TEST_SYNC_FD` is unset, this is a no-op — no fd lookup, no write,
  no behavior change.
- The entire hook is compiled only under `#ifdef BUILD_DEBUG` (the flag
  `build.c` already defines for debug/test builds producing `diredd`), so it
  does not exist at all in release builds.
- This is a separate channel from dired's stdout/pty output — it never
  writes into the terminal byte stream itself, so the pty's actual output
  stays pure rendering content for the terminal emulator to parse.

**Test binary**
- Tests run against the existing debug binary (`diredd`, built via
  `./builder debug`), not a new build variant.

**Pty session (Python)**
- Spawns `diredd` attached to a real pty, with `DIRED_TEST_SYNC_FD` set to
  the write end of a pipe created before spawn (the read end stays with the
  parent).
- Sets a fixed pty window size (proposed 80x24) applied via `TIOCSWINSZ`
  before dired starts, so `tb_width()`/`tb_height()` are deterministic for
  every test.
- After writing a step's keys, blocks reading one byte from the sync pipe,
  with a 5-second timeout. On timeout or on the child process exiting before
  signaling, fails the step with a message distinguishing "no response
  within 5s" from "process exited/crashed."
- Feeds all raw pty output continuously into a `pyte` screen/stream to
  reconstruct the terminal's visible grid.

**Key notation encoder (deep module, unit-testable in isolation)**
- Input: a list of strings, each either literal characters to send as-is, or
  a bracketed special-key name (e.g. `"<Enter>"`, `"<Esc>"`, `"<Down>"`,
  `"<Up>"`, `"<Left>"`, `"<Right>"`, `"<BS>"`, `"<Del>"`), matching the keys
  `translate_event()` (`src/dired.c`) actually distinguishes (`TB_KEY_ESC`,
  `TB_KEY_ENTER`, arrows, backspace/delete).
- Output: the raw bytes to write to the pty master.
- Pure function — no process or pty required to test it.

**Tagged-screen format (deep module, unit-testable in isolation)**
- Encodes a captured (or synthetic) screen's per-cell foreground, background,
  and bright-modifier state as nested tags wrapped around plain text:
  - Foreground color tags: `<red>`, `<green>`, `<yellow>`, `<blue>`,
    `<magenta>`, `<white>`, `<black>`; untagged text is default foreground.
    (Matches the exact color set `style_colors()`, `src/dired.c`, actually
    uses — no `<cyan>`, no bold/underline tags, since dired doesn't use
    them.)
  - `<bright>` wraps a color tag to indicate the `TB_BRIGHT` modifier (used
    today only for the "ignored" style).
  - `<bg-COLOR>` wraps a foreground tag (or plain text) to indicate a
    non-default background (used for selected/highlighted rows, which set
    both fg and bg).
  - No escaping mechanism for literal `<`/`>` in cell content — fixture
    filenames are test-author-controlled and assumed not to contain them.
- Provides both directions: format a captured screen into this tagged
  string form, and parse a tagged string (from the JSON `expect` field) into
  the same per-cell representation, so expected and actual can be diffed on
  identical structure.
- Pure transformation — testable by constructing screens/strings directly,
  no pty or process involved.

**Fixture builder (deep module, unit-testable in isolation)**
- Input: a declarative spec with (a) a file/directory tree (paths to
  contents) and (b) an optional `setup` list of git argv-arrays, e.g.
  `[["add", "a.txt"], ["commit", "-m", "init"]]`.
- Materializes (a) into a fresh temporary directory.
- If `setup` is non-empty: runs `git init` in that directory, then locally
  scoped `git config user.email test@test` / `git config user.name test`
  (local to that repo only, never touching global config or dired's own
  repo), then each `setup` entry as `git <args>` via `subprocess.run` with
  `shell=False` (no shell interpretation, no injection surface).
- Validates every `setup` entry's first argument is one of the whitelisted
  subcommands (`add`, `commit`) and rejects anything else before running
  anything — the JSON never specifies a program name, only arguments to a
  hardcoded `git` invocation.
- Conflicted (`UU`/`AA`) git states are intentionally not reproducible
  through this whitelist; that classification is already covered by
  `test/gitstatus_test.c`'s existing synthetic-porcelain-output tests.
- Testable by building a spec and asserting the resulting file tree and
  `git status --porcelain` output — no dired process involved.

**Test file format (`test/integration/*.json`)**
- Top-level: `fixture` (file/dir tree + optional `setup` list as above) and
  `steps` (ordered list).
- Each step: `{"keys": [...], "expect": [...]}` — `keys` in the notation
  above, `expect` optional (a list of row strings in the tagged format,
  checked against the captured screen at that point; omitted means "send
  these keys, don't assert yet").

**Runner (`test/integration/run.py`, orchestration — not a deep module)**
- Discovers all `*.json` files in `test/integration/`.
- For each: builds the fixture, spawns a pty session pointed at the fixture
  directory, drives each step (encode keys → send → wait for render signal
  → capture screen → compare against `expect` if present), and aggregates
  pass/fail.
- On mismatch: prints only the differing rows (row index, expected vs.
  actual in tagged form) rather than the full grid.
- Exits non-zero if any test fails.

**Tooling location**
- `test/integration/`: `run.py`, `requirements.txt` (pins `pyte`),
  `Makefile` (creates a venv, installs dependencies into it, runs the
  suite), and the `*.json` test files.
- Entirely separate from `build.c`/`./builder` — building/testing dired in C
  never requires Python, and running integration tests never invokes
  `./builder` directly (though it depends on `diredd` already being built,
  or builds it via a plain `make` recipe calling `./builder debug`).

**Scope**
- v1 exercises dired's internal behavior only. Any dired action that would
  launch an external process (run-shell-command mode, `$EDITOR`/pager
  launch) is out of scope — those child processes would inherit the same
  pty and introduce additional non-determinism (needing controlled
  `$EDITOR`, etc.) that isn't needed to close the git-status-coloring gap
  this PRD is motivated by.

## Implementation Chunks

Each chunk is independently testable and builds on the previous one; land
them in order.

1. **Sync hook** — the `#ifdef BUILD_DEBUG` render-signal hook in
   `src/dired.c` that writes to `DIRED_TEST_SYNC_FD` after `render()`, when
   set. C-only; no Python involved yet.
2. **Key notation encoder** — pure Python module translating vim-style key
   notation into raw pty bytes, matching the keys `translate_event()`
   distinguishes.
3. **Tagged-screen format** — pure Python module converting a pyte screen to
   nested-tag row strings and back, with round-trip and per-tag-kind tests.
4. **Fixture builder** — pure Python module materializing a declarative
   file/dir tree plus whitelisted (`add`/`commit`-only) git setup into a
   temp directory, with the non-whitelisted-command rejection test.
5. **Pty session + runner** — wires chunks 1–4 together into
   `test/integration/run.py` (spawn `diredd`, drive steps, report
   line-diffs), plus the first real `*.json` tests: the PRD 021 and PRD 024
   regression scenarios and basic navigation/preview/rename smoke tests.

## Testing Decisions

- Good tests here assert on dired's actual rendered output (or, for the
  isolated harness modules, on their pure input/output behavior) — never on
  internal implementation details of the harness itself.
- Unit tests (isolated, no pty/process, run under the harness's own Python
  test setup):
  - Key notation encoder: given a list of key-notation strings, asserts the
    exact output bytes, including all bracketed special keys the encoder
    supports.
  - Tagged-screen format: round-trip tests (format then parse recovers the
    same per-cell structure) plus specific cases for each tag kind (fg-only,
    `<bright>`, `<bg-COLOR>` wrapping fg, untagged/default).
  - Fixture builder: given a fixture spec, asserts the resulting file tree
    on disk, and for git fixtures, asserts `git status --porcelain --ignored`
    output matches the intended tracked/untracked/modified/deleted/ignored
    state; separately asserts that a non-whitelisted `setup` entry (e.g.
    `["push"]`) is rejected before any subprocess runs.
- Integration tests (the actual `test/integration/*.json` files, run via
  `run.py` against the real `diredd` binary): these are the deliverable
  itself, not meta-tests of the harness — the first ones written should
  specifically reproduce the PRD 021 and PRD 024 scenarios (untracked file
  nested in a tracked subdirectory; modified/new/deleted files visible after
  navigating below the repo root) as regression coverage, plus basic
  navigation/preview/rename smoke tests.
- Prior art: `test/gitstatus_test.c` already establishes the pattern of
  table-driven cases with a description string, input, and expected
  classification — the fixture builder's git-state unit tests should follow
  a similar table-driven shape where practical.
- Pty session (the process/pty lifecycle wrapper) is not itself
  unit-tested in isolation, by design — it's exercised implicitly by every
  integration test run, matching the framing in this PRD that it's
  orchestration/glue rather than a deep module.

## Out of Scope

- Driving dired's external-process launch paths (run-shell-command mode,
  `$EDITOR`/pager launches) — deferred to a later PRD if needed.
- Reproducing real git merge conflicts (`UU`/`AA`) through the fixture
  whitelist — conflicted-state classification stays covered at the unit
  level (`test/gitstatus_test.c`), not the integration level.
- Per-test terminal size overrides — v1 uses one fixed size for all tests.
- Any escaping mechanism for literal `<`/`>` characters in fixture file
  content or names.
- Wiring the integration suite into `./builder`/`build.c`, or into any CI
  configuration — this PRD covers the harness and its own `Makefile` only.
- A generic/extensible git-operation DSL beyond the `add`/`commit`
  whitelist; if a future scenario needs more, that's a follow-up decision,
  not something v1 should anticipate.

## Further Notes

- `CLAUDE.md` currently states "never run dired directly you cannot control
  a tui app" — that instruction reflects the absence of any way to drive the
  TUI programmatically today. Once this harness exists, that note may be
  worth revisiting (e.g. clarifying that the integration harness, not
  Claude interactively, is the supported way to exercise dired end-to-end),
  but changing `CLAUDE.md` itself is out of scope for this PRD.
- The debug build's existing `-fsanitize=address` flag (`build.c`) applies
  to `diredd` as used by this harness too; integration test runs will pay
  ASan's overhead, which is a reasonable tradeoff for the extra correctness
  signal but is worth knowing if step timeouts ever need adjusting.

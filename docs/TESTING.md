# Testing Strategy — dired

## Current state

* No `test/` directory, no automated tests.
* `build.c` already has a commented-out `build_test()` stub, copy-pasted from
  the `lust2D` project — the hook exists in the build system, nothing plugs
  into it yet.
* `src/dired.c` is a single ~360-line file: ncurses I/O, global mutable state
  (`entries[]`, `selected`, `current_path`, `mode`, `edit_buf`), and side
  effects (`fork`/`exec` vim, `rename`/`fopen`/`mkdir`/`rmdir`/`unlink`) are
  all interleaved in the same functions, driven from one big `main()` event
  loop.

## Why it's hard to test as-is

* No separation between pure logic and I/O — e.g. `delete_selected()` reads
  the confirmation key **and** performs the syscall **and** mutates global
  state in one function.
* Filesystem calls (`rename`, `mkdir`, `rmdir`, `unlink`, `opendir`) run
  directly against the process's cwd — nothing is injectable.
* `initscr()` / `getch()` / `refresh()` calls are woven through the same
  functions as the logic, so nothing can run headless.

## Proposed approach — three tiers

### 1. Pure unit tests (no refactor needed)

Functions already free of I/O and global state:

* `is_protected_name()`
* `mode_to_str()`

These can be tested today with the same `minitest.h` harness already used in
`lust2D` (vendor a copy into `dired/test/`).

### 2. Logic tests (small refactor: separate decision from action)

Several functions mix "decide what to do" with "do it via ncurses/syscalls":

* Selection clamping on `KEY_UP`/`KEY_DOWN` — currently inline in `main()`.
  Extract e.g. `int clamp_selection(int selected, int entry_count, int
  virtual_line)` so the bounds logic is testable without a terminal.
* `start_edit` / `cancel_edit` / `validate_edit` implement a state machine
  over `mode`, `edit_buf`, `virtual_line`, `selected`. Moving that state into
  an explicit `AppState` struct (instead of file-static globals) makes the
  transitions testable without ncurses or the filesystem.
* The path-building in `validate_edit` (`snprintf(path, ...)`) can be tested
  in isolation once separated from the actual `rename()`/`fopen()`/`mkdir()`
  calls.

### 3. Filesystem integration tests (real temp dirs, no ncurses)

* `load_directory()` already takes a path argument and never touches
  ncurses, so it can be tested today: create a temp directory (`mkdtemp`)
  with known files/subdirs and assert `entry_count` and `Entry` contents.
* Once delete/create/rename logic is separated from the confirmation prompt
  (`getch()`) and from `draw()`, the same temp-dir approach validates
  `delete_selected` / `validate_edit` against a real filesystem, with no
  dependency on cwd or a real terminal.

### Out of scope for automated tests

`draw()` and the raw `main()` event loop (ncurses rendering + key dispatch)
— not worth unit testing. Validate manually, or later with a scripted
terminal tool (`tmux send-keys` / `expect`) as a smoke test.

## Concrete next steps

1. `mkdir test/`, vendor `minitest.h` (copy from `lust2D`).
2. Uncomment `build_test()` in `build.c`, point `src_dir` at `test/`, link
   against `libdiredd.a` (already produced by `build_lib(1)`) instead of
   duplicating logic.
3. Start with tier 1 (`is_protected_name`, `mode_to_str`) to get the harness
   wired end-to-end.
4. Add tier-3 tests for `load_directory` against `mkdtemp()` fixtures — no
   refactor required.
5. Only then tackle the tier-2 `AppState` refactor — bigger surface, higher
   payoff for regression safety on the rename/create/delete flows.

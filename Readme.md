# Terminal File Explorer

A lightweight terminal-based file manager written in C using termbox2 (vendored — no system terminal library required). Designed for fast navigation and file editing over SSH or any terminal session.

## Features

* Navigate directories with arrow keys.
* Enter a `.zip` or `.tar`/`.tar.gz`/`.tgz`/`.tar.bz2`/`.tbz2`/`.tar.xz`/`.txz`/`.tar.Z` archive with →/Enter to browse its contents like a directory.
* Open and edit files with Vim directly from the interface.
* Rename files and directories.
* Create new files or directories (via a temporary virtual line).
* Delete files and directories with confirmation — moved to `~/.trash` by default, or permanently with a separate key.
* Preview a file's contents (paged as text, or hex-dumped if binary) without leaving the app.
* Configure custom preview commands per file extension (e.g. render markdown or images) via `~/.config/dired`.
* Run an ad-hoc shell command against the current directory without leaving the app, with Up/Down recall of that folder's previously run commands.
* Filter the listing live by filename, either a plain substring or an extended regex.
* Recursively glob the current directory tree by filename, either a plain substring or an extended regex.
* Sort by name, date, size, or extension, with independent directory grouping.
* Minimal, fast, and works entirely in the terminal.
* Simple modal interface: navigation mode and edit mode.

## Usage

1. Clone the repository and enter it:

```bash
git clone <repo_url>
cd dired
```

2. Build the project and run the file explorer (a plain C compiler is enough — no `libncurses-dev` or other system package needed):

```bash
cc build.c -o builder && ./builder debug clean dired
```

3. Run over SSH if needed:

```bash
ssh user@remotehost 'cd /path/to/dired && ./builder debug clean dired'
```

4. Controls:

Press `h` inside dired at any time to see this same reference without leaving the app.

```
Two modes: Navigation (default) and Selection (v). Selection mode marks
multiple entries for one batch action.

Navigation mode
  up/down      Navigate          f            Filter (plain)
  left         Parent dir        F            Filter (regex)
  right/Enter  Open/enter        g            Glob (plain)
  r            Rename            G            Glob (regex)
  n            New file/dir      v            Selection mode
  :!           Run command       space        Preview
  c            Yank copy         s            Cycle sort
  m            Yank move         d            Cycle group
  p            Paste             o            Next page
  Backspace    Trash             a            Toggle hidden
  x            Delete            h            Help
  Esc          Cancel yank       q            Quit

Selection mode (v)
  v            Exit select       t            Select all/none
  r            Range-select      space        Toggle mark
  Esc          Cancel select     up/down      Extend range
  c/m/p        Batch yank/paste  Backspace/x  Batch delete

Command prompt (:!)
  up/down      Recall history
```

## Testing

Run the C unit test suite:

```
make unit
```

(equivalent to `./builder clean debug test`)

Run the full test suite — unit tests, then the integration suite (drives
the real `diredd` binary under a pseudo-terminal and asserts on rendered
screen output, including git-status colors, via JSON test files in
`test/integration/`):

```
make test
```

Requires `python3` and `make`; `make test` installs its own virtualenv
and pinned dependencies automatically (or run `make install` first to
do just that step).

## Per-Extension Preview Commands

By default, previewing a file (Space) pages its text through `less -R`, or hex-dumps it through `hexdump -C` piped into `less -R` if it looks binary. You can override this per file extension by editing `~/.config/dired` — the file (and `~/.config` itself, if missing) is created automatically the first time you run dired, pre-filled with a commented-out example config to get you started (every line commented out, so it has no effect until you uncomment something).

Each line is:

```
ext=command arg1 arg2 $FILE
```

* `ext` has no leading dot (e.g. `md`, not `.md`).
* `$FILE` is replaced with the real path of the file being previewed — it can appear more than once and can be embedded inside a larger argument (e.g. `--file=$FILE`).
* `$COL` is optional and is replaced with the terminal's current column width, useful for sizing output (e.g. an image renderer) to fit the screen.
* If the environment supports it, your command's stdout/stderr run attached to a real pseudo-terminal rather than a plain pipe, so TTY-aware tools (e.g. `chafa`, which checks `isatty()` to decide render quality) produce full-quality output instead of a degraded fallback. Where pty allocation isn't available, this falls back silently to the old plain-pipe behavior — no error shown, nothing to configure.
* Blank lines and lines starting with `#` are ignored, so you can space out and comment your rules.
* Matching is case-insensitive and checks whether the filename ends in `.` + `ext` — so `tar.gz` works as a key without special-casing. When more than one rule could match, the first one listed wins, so order rules deliberately (e.g. put `tar.gz=` above `gz=` if you want the more specific one to take precedence).
* Files with no matching rule preview exactly as before (text via `less -R`, binary via `hexdump -C | less -R`).
* This also applies to files previewed inside an archive, not just files on disk.

A malformed line (missing `=`, an empty key, or a value missing `$FILE`) is a startup error — dired refuses to start and reports the offending line.

Example config:

```
# ~/.config/dired
md=glow $FILE
jpg=chafa --size=$COLx40 $FILE
png=chafa --size=$COLx40 $FILE
```

## Example Interface

```
Path: /home/user/project
Rename:

drwxr-xr-x    128 src
-rw-r--r--    512 main.c
-rw-r--r--     64 README.md
-rw-r--r--     32 config.h
>           new_file.txt      <- virtual line when creating a file

↑↓: Navigate  ←: Parent  →/Enter: Open  r: Rename  n: New  Backspace: Delete  q: Quit
```

* The `>` indicates the current selection.
* The last line (virtual line) is used when creating a new file or directory — the user types the name directly.
* Files and directories are displayed with permissions and sizes.
* Inside an archive listing, entries show `----------`/`d---------` in place of permissions, since archive tools don't always report real permission bits.

## Purpose

This tool provides a **simple and fast way to browse directories and edit files in a terminal**, particularly useful when working on remote systems via SSH. It focuses on minimalism and speed, allowing quick file management without a full-featured file manager.

## Note for AI agents working in this repo

Never launch `dired`/`build/dired` directly (interactively or headlessly) to check whether a change works — it's a full-screen terminal UI (`tb_init()`/`tb_poll_event()`), and an agent has no way to see or interact with that UI. Piping fake keystrokes at it or background-and-timeout-killing it is not a substitute for real interaction, and a timeout-triggered exit is not a pass/fail signal for TUI behavior.

This includes `./builder clean dired` / `./builder debug clean dired` — despite looking like a build command, passing `dired` as an argument to `./builder` also **executes** the freshly-built binary (see `build.c`'s `BUILD_RUN_CMD("./build/"TARGET_DIRED)` call), which hangs forever waiting for a keypress. `build_lib()` already compiles and links `build/dired`/`build/diredd` unconditionally, so to confirm the binary builds and links, run `./builder clean` (release) or `./builder debug clean` (debug) — never append `dired` after `clean`/`debug clean`, that's only for a human at a real terminal who wants to run the app.

Verify changes via code review, unit tests of the pure helpers `execute_*`/`update`/etc. call, and — for non-interactive startup-only behavior (e.g. config loading that happens before `tb_init()` runs) — a scratch-cwd script that inspects files/stderr/exit code produced *before* the TUI starts (the debug binary `diredd` resolves trash/history/preview-config paths from the process's cwd rather than `$HOME`, so isolate by running from a scratch directory, not by setting `$HOME`). Never treat anything that happens after `tb_init()` as something you actually observed. For behavior that requires watching the UI render or respond to keypresses, rely on the user for interactive testing, and say plainly what still needs a human at a real terminal.

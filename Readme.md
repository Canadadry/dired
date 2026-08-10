# Terminal File Explorer

A lightweight terminal-based file manager written in C using termbox2 (vendored — no system terminal library required). Designed for fast navigation and file editing over SSH or any terminal session.

## Features

* Navigate directories with arrow keys.
* Open and edit files with Vim directly from the interface.
* Rename files and directories.
* Create new files or directories (via a temporary virtual line).
* Delete files and directories with confirmation — moved to `~/.trash` by default, or permanently with a separate key.
* Preview a file's contents (paged as text, or hex-dumped if binary) without leaving the app.
* Run an ad-hoc shell command against the current directory without leaving the app.
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

```
↑↓: Navigate
←: Go to parent directory
→ / Enter: Open file or enter directory
r: Rename selected file/directory
n: Create a new file or directory (trailing / for a directory)
Space: Preview selected file (text pages, binary is hex-dumped)
:: Run a shell command (prefix with !, e.g. !unzip $FILE), output paged. $FILE is the selected entry's path
s: Cycle sort key/direction (name, date, size, extension — asc/desc)
d: Cycle directory grouping (first, last, mixed)
a: Toggle hidden files (dotfiles hidden by default)
Backspace: Move selected file/directory to trash (~/.trash)
x: Permanently delete selected file/directory (bypasses trash)
q: Quit
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

## Purpose

This tool provides a **simple and fast way to browse directories and edit files in a terminal**, particularly useful when working on remote systems via SSH. It focuses on minimalism and speed, allowing quick file management without a full-featured file manager.

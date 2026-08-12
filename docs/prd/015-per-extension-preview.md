---
title: "Per-extension preview commands via config file"
description: "The preview key always runs the same hardcoded more/hexdump logic regardless of file type, so there's no way to view a markdown file rendered, an image as art, or a JSON file pretty-printed without leaving dired."
status: done
---

## Problem Statement

Today, pressing the preview key (space) on a regular file always does one of
two hardcoded things: pages the raw text through `more`, or — if the sniffed
content looks binary — pipes a `hexdump -C` dump through `more`
(`005-hex-preview.md`). This was a deliberate scope cut: `002-file-preview.md`
explicitly listed "making the pager configurable" as out of scope. There is
no way to tell dired "when I preview a `.md` file, render it with `glow`" or
"when I preview a `.jpg`, show it with `chafa`" — every file type gets the
same generic treatment regardless of whether a better-suited viewer is
installed and available.

## Solution

Add a config file, `~/.config/dired`, that maps file extensions to preview
commands. Each line is `ext=prog arg1 arg2 $FILE` (no leading dot on `ext`).
When the selected file's name ends in `.` + one of the configured extensions,
dired runs that command (with `$FILE` substituted for the real path, and
`$COL` substitutable for the terminal's current column width, so a command
like an image renderer can size its output to fit) and pipes its output
through `more`, instead of running the existing sniff-based text/hex logic.
Files with no matching extension fall through to exactly today's behavior,
unchanged. Lines starting with `#` are comments and are ignored, so an
example/annotated config can ship as documentation. The config file is
created empty on first run if it doesn't exist; a malformed line is treated
as a startup-fatal misconfiguration (dired refuses to start and reports the
bad line) rather than silently ignored, since silent ignoring would make a
typo'd extension rule impossible to debug.

This also fixes a related gap: previewing a file inside an archive
(`014-read-archive.md`'s archive-member preview) extracts the member to a
temp file first, and that temp file currently has no extension at all
(`mkstemp`'s `XXXXXX` template has nothing after it), so extension rules
could never match an archive member no matter how it's configured. This PRD
changes archive-member extraction to preserve the member's real filename so
extension rules apply there too.

## User Stories

1. As a user, I want to map a file extension to a custom preview command in a config file, so that I can view a file type with a tool better suited to it than the generic pager/hexdump.
2. As a user, I want `~/.config/dired` created automatically (with `~/.config` created too, if needed) the first time I run dired if it doesn't already exist, so that I have a starting point to edit without manually creating directories.
3. As a user, I want each config line to be a simple `ext=command $FILE` rule, so that I don't need to learn a complex config syntax.
4. As a user, I want extension matching to be case-insensitive, so that `.JPG` and `.jpg` are treated the same.
5. As a user, I want extension matching to work by "filename ends with `.` + key" rather than true extension parsing, so that compound suffixes like `tar.gz` just work as a config key without any special-casing.
6. As a user, when multiple rules could match the same file (e.g. both `gz=` and `tar.gz=` are configured), I want the first matching rule in the file (top to bottom) to win, so that I have explicit, predictable control over precedence by ordering my rules.
7. As a user, I want blank lines in the config file to be ignored, so that I can space out my rules for readability.
8. As a user, I want lines starting with `#` to be treated as comments and ignored, so that I can annotate my rules and ship/read an example config with explanations.
9. As a user, I want a malformed config line (missing `=`, empty key, or a value with no `$FILE` token) to stop dired from starting and print a clear error identifying the bad line, so that I notice and fix a typo'd rule immediately instead of it silently never firing.
10. As a user, I want `$FILE` in a rule's command to be replaced with the real path of the file I'm previewing, so that the configured program actually receives the file to open.
11. As a user, I want `$FILE` to be substitutable even when embedded inside a larger argument (e.g. `--file=$FILE`) and even if it appears more than once, so that I'm not limited to `$FILE` being its own standalone argument.
12. As a user, I want an optional `$COL` token in a rule's command to be replaced with the terminal's current column width, so that a configured command (e.g. an image renderer or a formatter that wraps text) can size its output to fit the screen instead of guessing or defaulting to 80 columns.
13. As a user, I want `$COL` to be optional — a rule that never references it should work exactly as before — so that I only deal with terminal width when a command actually benefits from it.
14. As a user, I want a file whose extension isn't configured to preview exactly as it does today (text via `more`, binary via `hexdump -C | more`), so that adding a config file doesn't change behavior for anything I haven't explicitly configured.
15. As a user, I want a configured command's output paged through `more` the same way text preview already is, so that I get the same familiar space/enter/`q` controls regardless of file type.
16. As a user, I want extension matching to happen before any binary/text sniffing, so that a configured binary-ish file type (e.g. images) isn't first misclassified by the content sniff.
17. As a user, I want to preview an archive member whose real name has a configured extension and have the matching rule apply, so that per-extension preview works consistently whether I'm browsing a real directory or inside an archive.
18. As a user, I want directories to still be a no-op for preview regardless of config, so that extension rules never change that existing guard.
19. As a developer, I want config parsing to be a pure function (text in, rule list or parse error out) with no filesystem or process access, so that every parsing rule (blank-line skipping, comment-line skipping, malformed-line detection, `$FILE` requirement) can be verified without touching disk.
20. As a developer, I want extension matching to be a separate pure function from parsing, so that precedence and case-insensitivity rules can be tested independently of the config file's text format.
21. As a developer, I want `$FILE`/`$COL` substitution to be a separate pure function from matching, so that argv construction (whitespace splitting, per-token substring replacement) can be tested independently of which rule was selected.
22. As a developer, I want the parsed rules held in a plain static array at file scope in `dired.c`, not threaded through `Model`/`Msg`/`Cmd`, so that this feature — like `002`/`005` before it — adds zero new `Model` state; the rules are read-only for the life of the process.
23. As a developer, I want config loading (create-if-missing, read, parse-or-exit) to happen once in `main()` before `tb_init()`, so that a fatal config error is reported on a clean terminal instead of corrupting the TUI.
24. As a developer, I want the configured command executed directly via `execvp` with an argv array, never via a shell, so that a filename can never be interpreted as shell syntax — consistent with the "never a shell" rule already established for the hex-dump pipeline in `005-hex-preview.md`.
25. As a developer, I want the configured-command-to-`more` pipe to reuse the existing `run_piped` helper unchanged, so that this feature doesn't duplicate the pipe/fork/exec bookkeeping already built and proven for the hex-dump path.
26. As a developer, I want fixed, small caps on the number of rules and on rule/line length (consistent with this codebase's existing static-buffer conventions like `MAX_ENTRIES`/`PATH_MAX_LEN`), so that config parsing needs no dynamic allocation.
27. As a developer, I want the exec-time argv buffer (after `$FILE`/`$COL` substitution) sized independently from the stored rule-template buffer, so that substituting a long real path into a short template never truncates or overflows.
28. As a developer, I want the terminal column width captured via `tb_width()` before `tb_shutdown()` runs in `execute_preview`, so that `$COL` reflects the real on-screen width at the moment preview was triggered, the same way `Model.term_width` is already cached from `tb_width()` at startup and on resize.
29. As a developer, I want archive-member extraction changed from a flat `mkstemp` file to an `mkdtemp`-created private directory containing a file with the member's real basename, so that the member's real extension survives into the temp file dired hands to `execute_preview`.
30. As a developer, I want archive-member cleanup to remove both the temp file and its now-added parent temp directory, so that this change doesn't leak an empty directory per archive-member preview.
31. As a user, if `~/.config/dired` (or its parent `~/.config`) can't be created (e.g. permission denied, read-only `$HOME`), I want dired to still start normally with no extension rules active, so that a filesystem hiccup on a config file that has no user-visible content yet doesn't block me from using the app at all.

## Implementation Decisions

- **Config file location**: `~/.config/dired` (a file, not a directory), resolved via `getenv("HOME")` — same precedent already used in `trash.c`. This is the first config file dired has ever read.
- **Startup behavior** (in `main()`, before `tb_init()`):
  - `mkdir -p`-equivalent creation of `~/.config` if missing, then create `~/.config/dired` empty if missing. If either creation step fails (permissions, read-only `$HOME`), dired proceeds with zero rules loaded — this failure path is silent, not fatal, since there's no user-authored content at risk.
  - If the file exists (or was just created empty), read it, and parse via the pure parser described below.
  - A parse error (malformed line) is fatal: print an error to stderr identifying the offending line and exit with a non-zero status, before `tb_init()` runs. This is a deliberate exception to this codebase's otherwise-universal "fail silently on I/O/exec problems" convention (established across `002`/`005`'s pager/editor-launch failure handling) — the point of failing loudly here is that a silently-ignored malformed rule would be invisible and undebuggable.
- **Format**: one rule per line, `ext=command arg1 arg2 $FILE`. `ext` has no leading dot. Blank lines are skipped. Lines whose first non-whitespace character is `#` are comments and are skipped (this is the one case where whole-line content is ignored regardless of what follows). Any other line must contain `=`, have a non-empty key, and have a value containing the literal substring `$FILE` at least once — any violation is the fatal parse error above.
- **New pure module additions in `helpers.c`/`helpers.h`** (alongside `is_binary_content`, `is_protected_name`):
  - **Parser**: takes the config file's raw text and produces either a list of `{suffix, argv-template}` rules or a parse error (with line number/content). Blank lines and `#`-prefixed comment lines are both skipped before the `=`/`$FILE` validation runs. No filesystem access — the caller in `dired.c` does the `fopen`/`fread`.
  - **Matcher**: takes a filename and the parsed rule list, returns the first matching rule (or none). Match rule: case-insensitive `filename ends with "." + suffix"`; first match in declaration order wins — no "most specific suffix" logic, ordering is entirely the user's responsibility.
  - **Argv builder**: takes a matched rule's argv template, the real file path, and the current terminal column width, and returns the final argv — the template is whitespace-split into tokens (no quoting support; an argument containing a space cannot currently be expressed), and every occurrence of the literal substrings `$FILE` and `$COL` within each token is replaced with the real path and the stringified column width, respectively. `$COL` is optional — a template with no `$COL` token simply never uses the width argument.
- **Storage**: the parsed rule list lives in a static, file-scope array in `dired.c`, populated once at startup by the loading step above. It is not part of `Model`, `Msg`, or `Cmd` — consistent with `002-file-preview.md`/`005-hex-preview.md` both explicitly avoiding new `Model` state for preview.
- **Sizing** (fixed caps, static allocation, no dynamic memory — consistent with existing constants like `MAX_ENTRIES 1024`, `PATH_MAX_LEN 1024`):
  - A cap on total rule count (e.g. 64).
  - A cap on stored rule-template length (e.g. 256 bytes per line) and on argv token count per rule (e.g. 16) — sized only for the literal text the user types, no filename included yet.
  - A separately-sized exec-time buffer, used only after `$FILE` substitution, large enough for a template token plus a full `PATH_MAX_LEN` path — since substitution is a string replacement, not a fixed-width slot, this must not reuse the smaller template cap.
- **`execute_preview` change**: before the existing sniff-and-branch logic, check the matcher against the static rule list.
  - Match → capture the current terminal width via `tb_width()` (called before `tb_shutdown()`, same moment `Model.term_width` would already be current), build the final argv via the argv builder using that width for `$COL`, and run it through the existing `run_piped(argv, pager_argv, NULL)` helper (the same one already used for the `hexdump -C | more` pipeline) — no new pipe/fork/exec helper needed.
  - No match → fall through unchanged to the existing sniff (`is_binary_content`) → `more` (text) / `hexdump -C | more` (binary) logic.
- **Archive-member extraction (`extract_member_to_tmp`) change**: switches from a flat `mkstemp("%s/dired-archive-XXXXXX")` file to `mkdtemp("%s/dired-archive-XXXXXX")` creating a private temp directory, inside which a file is created carrying the member's real basename (e.g. `/tmp/dired-archive-XXXXXX/notes.tar.gz`). This preserves the exact original name — including multi-part suffixes — with no extension-parsing logic needed. `execute_open_archive_member`'s cleanup adds an `rmdir` of the temp directory after the existing `remove()` of the file.
- **No shell, ever**: the configured command is invoked directly via `execvp` with the built argv, exactly like every other `exec`-family call in this codebase (`005-hex-preview.md`'s precedent) — never `sh -c`, since the filename is untrusted input flowing into `$FILE`.
- **Help text**: unchanged. The existing `"space: Preview"` help-bar segment already covers this — same precedent as `005-hex-preview.md`, which also changed preview's behavior without touching help text.
- **No new `Msg`, `Cmd`, or `AppMode`.** The entire feature is: a new config-loading step in `main()`, three new pure helpers, a new branch at the top of `execute_preview`, and a change to how archive-member temp files are named.

## Testing Decisions

- Good tests here assert on pure function return values only, never on terminal output, real filesystem state, or by shelling out to external programs — consistent with every existing convention in this codebase (`is_binary_content`, `is_protected_name`, `gitstatus`-style modules).
- **Parser**: table-driven tests in `test/helpers_test.c`, same `Case[]` + loop style as `test_is_binary_content`. Cases: single valid rule, multiple rules, a blank line skipped, a `#`-comment line skipped, a comment line placed between two valid rules, a line missing `=`, an empty key, a value with no `$FILE` token, and a rule count at/over the fixed cap.
- **Matcher**: table-driven tests. Cases: exact match, no match, case-insensitivity (`.JPG` file vs. `jpg=` rule), first-match-wins precedence when two rules could both match (e.g. `gz=`/`tar.gz=` against `archive.tar.gz`, tested both orderings), and an empty rule list.
- **Argv builder**: table-driven tests. Cases: `$FILE` as its own token, `$FILE` embedded in a larger token, `$FILE` repeated across multiple tokens, `$COL` substituted alongside `$FILE` in the same rule, a rule with no `$COL` token at all, and a path long enough to exercise the separately-sized exec-time buffer without truncation.
- **Config loading in `main()`** (mkdir/create-if-missing/read/parse-or-exit) is impure I/O and startup-`exit()` logic and is explicitly **not** unit tested, matching the existing convention that excludes all filesystem/process code in `dired.c` from unit tests. Verified manually: missing file gets created empty and dired starts with no rules; a valid file loads silently; a malformed file aborts startup with a message identifying the bad line before any TUI output appears.
- **`execute_preview`'s new matched-rule branch and the archive-member `mkdtemp` change** are fork/exec/filesystem code and are explicitly not unit tested, matching the existing convention that excludes `execute_preview`'s sniff/pager branches and `execute_open_archive_member` from unit tests. Verified manually: a configured extension runs the configured command piped through `more`; an unconfigured extension falls through to today's sniff-based behavior unchanged; previewing an archive member with a configured extension now also triggers the configured command; quitting `more` early in any case returns cleanly to the listing with no lingering processes or leaked temp files/directories.

## Out of Scope

- Quoted/escaped arguments in a rule's value (e.g. an argument containing a space) — the value is whitespace-split with no tokenizer; adding one is explicitly deferred as unneeded complexity for now.
- "Most specific suffix wins" precedence (e.g. `tar.gz` automatically beating `gz` regardless of declaration order) — precedence is purely first-match-in-file-order.
- Inline (end-of-line, trailing `#`) comments — only a line whose first non-whitespace character is `#` is a comment; `#` appearing later on a rule line is not special-cased.
- Hot-reloading the config file without restarting dired — rules are loaded once at startup into a static array, not re-read during a session.
- Any other substitution tokens beyond `$FILE` and `$COL` (e.g. terminal row count, file size, mime type) — deferred until a concrete need for one arises.
- Any change to the pager itself — matched and unmatched files alike still page through `more`; this PRD only changes what feeds `more` (or bypasses it) before that point.
- A UI/in-app way to edit or view the current config — it's a plain text file the user edits externally.
- Any config outside of preview commands (sort order, colors, keybindings, etc.) — this PRD is scoped entirely to per-extension preview.

## Further Notes

Depends on `002-file-preview.md` (the `CMD_PREVIEW`/`execute_preview` flow) and `005-hex-preview.md` (the sniff/hexdump/`run_piped` machinery this PRD's no-match fallback and pipe reuse both build on), and touches `014-read-archive.md`'s archive-member preview path (`extract_member_to_tmp`/`execute_open_archive_member`). This PRD explicitly revisits and reverses `002-file-preview.md`'s "making the pager configurable... out of scope" decision, now that there's a concrete need (extension-aware external viewers) and a config-file mechanism that didn't exist for `002`.

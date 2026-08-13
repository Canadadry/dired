---
title: "Report exec failures for preview/pager child processes"
description: "When a preview or pager command fails to exec (not installed, bad path, etc.), the failure is currently invisible: the app just shows nothing, with no indication of what went wrong or why."
status: needs-grilling
---

## Problem Statement

Several operations in dired already surface a real OS-level failure to the user: `run_argv` (used by copy/move/delete/extract/archive-listing) captures a failed child's stderr into a pipe and reports it through the existing `msg_failed`/`MODE_ERROR` mechanism, so a bad command shows up as a visible, unmissable error in the app's message line. Preview does not have this. `run_piped` (used today for the `hexdump -C | more` binary-preview pipeline, and about to be used more broadly by both `015-per-extension-preview.md`'s configured commands and `unified-preview-paging.md`'s pty/`less -R` mechanism) and the direct-exec preview paths (`execute_launch_editor`'s `vim` launch, and plain-text preview's direct pager exec) have no equivalent: if the child's `execvp`/`execlp` fails — the configured command isn't installed, `less`/`more`/`hexdump` is missing, a typo'd command in `~/.config/dired` — the child just calls `_exit(EXIT_FAILURE)` silently. From the user's perspective, pressing preview on a file just does nothing, with zero indication of why.

## Solution

Extend the same pattern `run_argv` already uses successfully — an `O_CLOEXEC`-backed pipe that a child writes its exec failure into before exiting, which the parent reads and folds into a `msg_failed(...)`-reported, impossible-to-miss error — to the preview/pager code paths that currently lack it. This is not a new mechanism; it's applying an already-proven one to the places it's missing.

## User Stories

*(Draft — this PRD has not yet been through a grilling session; the stories below reflect what's been established in adjacent conversations, not a resolved design.)*

1. As a user, when a configured preview command in `~/.config/dired` fails to run (not installed, typo'd path), I want to see a clear error message in dired, so that I know what went wrong instead of seeing nothing happen.
2. As a user, when the built-in pager (`more`/`less`) or `hexdump` itself is missing, I want the same clear error, so that a missing system dependency isn't mistaken for a dired bug.
3. As a developer, I want this to reuse the existing `run_argv` errpipe/`report_exec_failure` pattern rather than invent a new one, so that error-capture stays consistent across the codebase.
4. As a developer, I want this to cover `run_piped`'s two children (producer and pager) and the direct-exec preview paths, so that the same gap doesn't persist in some preview paths while being fixed in others.

## Implementation Decisions

*(Draft — grounded in the existing code, but the exact scope boundaries below still need to be confirmed in a grilling pass.)*

- The existing `run_argv` pattern: before forking, open a pipe; the child `dup2`s the write end onto `STDERR_FILENO` (or a dedicated fd); on `execvp` failure, the child calls `report_exec_failure(argv[0])` (already exists — formats `"<prog>: <strerror(errno)>\n"` and writes it) before `_exit`; the parent reads the pipe after the fork, and a non-empty read means exec failed, which gets folded into a `msg_failed(...)` call. On successful exec, the pipe's write end closes automatically (the fd was marked close-on-exec, or is otherwise no longer written to), so the parent's read returns empty.
- Candidate call sites needing this treatment, identified directly from the current codebase: `run_piped` (both the producer and the pager child — open question: does a pager-exec failure deserve the same visible error as a producer-exec failure, or should the two be distinguished?), `execute_launch_editor` (`vim` launch), and the direct-exec plain-text pager path. `unified-preview-paging.md`'s new `run_via_pty`/`run_via_pipe` functions (not yet built) should get this from the start rather than needing a follow-up fix, since that PRD explicitly deferred this exact problem here.
- Error message format/wording should match the existing `msg_failed("<operation>: %s", ...)` convention already used everywhere else (e.g. `"preview: %s"`), for consistency with rename/delete/copy/move/extract error messages the user already sees.

## Testing Decisions

*(Not yet resolved — needs grilling.)* Likely candidates, following `015-per-extension-preview.md`'s and `unified-preview-paging.md`'s precedent: this is fork/exec code, so any coverage would be integration-style (real subprocess, e.g. exec a deliberately-nonexistent binary and assert the captured error text), not a mock. Whether this warrants new test coverage or stays in the "verified by code review + manual testing" bucket (like `run_piped` itself today) is an open question.

## Out of Scope

- Detecting failures that happen *after* a successful exec (e.g. the command runs but produces wrong/garbled output, like the original `chafa`-via-`more` problem `unified-preview-paging.md` addresses) — this PRD is specifically about the exec-call-itself-failed case.
- Any change to what happens on success — this only changes behavior when a child fails to start.

## Further Notes

This PRD was intentionally split off from `unified-preview-paging.md` during that PRD's design discussion, to keep that one scoped to the pty/pager mechanism. It has **not** been through a grilling session — the user explicitly asked to defer that. Before this is ready for implementation, a grilling pass should resolve at minimum: the exact list of call sites in scope (does `execute_launch_editor`'s `vim` launch really need this, given the TUI is already torn down when it runs and a missing `vim` is a fairly extreme edge case?), whether both children of `run_piped` need distinct error handling or a shared one, and the testing-coverage question left open above.

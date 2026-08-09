---
title: "Hex view for binary files"
description: "Pressing the preview key on a binary file currently just rejects it with an error, giving the user no way to glance at its content the way text preview already allows."
status: needs-triage
---

## Problem Statement

Today, pressing the preview key (space) on a regular file either pages its
text content via `more`, or — if the file is binary — refuses outright with
`preview: binary file` and no further recourse. To look inside a binary file
at all (an executable, an image, a core dump, an unfamiliar file with no
extension), the user has no in-dired option; they have to drop to a shell and
run their own hex dump tool. This was a deliberate, explicit deferral in
`002-file-preview.md`, which extracted binary detection into its own pure
helper specifically so a follow-up PRD could build a hex view on top of it
without re-deriving that logic.

## Solution

The same preview key now hex-views a binary file instead of rejecting it: no
new keybinding, no new mode, no new menu — the existing "preview" action
becomes content-aware in exactly the way `more` already handles text. The
byte contents are formatted as a canonical hex+ASCII dump and paged with the
same external pager already used for text preview, so the user gets the
identical mental model (press space, look, press `q` to return to browsing)
regardless of whether the file turns out to be text or binary.

## User Stories

1. As a user, I want to press the preview key on a binary file and see its bytes, so that I can identify or inspect it without leaving dired for a shell.
2. As a user, I want the hex dump to page with the same familiar controls (space/enter to page, `q` to quit) I already use for text preview, so that I don't have to learn a second set of controls just because the file happened to be binary.
3. As a user, I want to be able to quit out of a large binary file's hex view early without waiting for the whole file to be processed, so that glancing at a huge file doesn't hang dired.
4. As a user, I want the file listing to return to view, refreshed, after I quit the hex view, so that browsing continues exactly where text preview and editor-launch already leave it.
5. As a user, I want pressing the preview key on a directory to still do nothing, so that hex-viewing doesn't change any of the existing guard behavior around non-regular-file selections.
6. As a developer, I want the hex-view branch to reuse the exact same `Msg`/`Cmd`/`update()` flow as text preview, so that no new `Model` state, `AppMode`, `Msg`, or `Cmd` variant is introduced just for this feature.
7. As a developer, I want the binary/text decision to keep using the existing pure `is_binary_content` helper unchanged, so that this PRD doesn't re-derive or duplicate detection logic already shipped and tested in `002-file-preview.md`.
8. As a developer, I want the dump tool invoked without any shell interpolation of the filename, so that a maliciously or accidentally crafted filename can never be interpreted as shell syntax.
9. As a developer, I want the two-process pipe/fork/exec bookkeeping isolated behind a small helper rather than inlined into the preview branch, so that the preview code's control flow stays readable and mirrors how the existing stderr-capturing fork/exec helper is already isolated from its callers.
10. As a developer, I want both child processes properly waited on, so that quitting the pager early never leaves a zombie process behind.

## Implementation Decisions

- **Trigger**: no new key, `Msg`, or `Cmd`. The existing preview key (space) still produces the existing `CMD_PREVIEW` command through the existing `handle_nav` guard (out-of-range or non-regular-file selection is still a no-op). All of this is unchanged from `002-file-preview.md`.
- **Detection**: unchanged. The existing sniff-then-check step (read up to the existing fixed sniff length, then call the existing pure `is_binary_content` helper) still decides binary vs. text. No changes to that helper's signature, behavior, or tests.
- **Binary branch behavior change**: previously, tripping the binary check returned a failure message and did nothing else. Now, tripping the binary check launches a two-stage pipeline instead: a hex-dump program formats the file's bytes, piped into the same external pager already used for text preview. This supersedes user story 4 from `002-file-preview.md` ("binary file rejected with a clear error message") — that behavior is retired in favor of hex-viewing.
- **Dump format**: canonical offset + hex + ASCII sidebar format (the same shape produced by `hexdump -C`), unconfigurable — same hardcoding convention `002-file-preview.md` already established for the pager choice itself.
- **No shell, ever**: both stages are invoked directly via `exec`-family calls with argument vectors, never via a shell or any string-interpolated command line, since the filename is untrusted input. This mirrors the "never a shell" precedent already established by the existing stderr-capturing fork/exec helper used by copy/move.
- **Piping helper**: a new small helper, argument-vector-in/argument-vector-in shaped like the existing single-command fork/exec helper, wires one child's stdout to a pipe and a second child's stdin to the same pipe, closes both pipe ends in the parent, and waits on both child pids before returning. It has exactly one call site (dump piped to pager) — it is not being built as speculative infrastructure for hypothetical future pipelines, only to keep the preview branch's control flow readable given that two processes and pipe bookkeeping is meaningfully more code than any other branch in that function.
- **No size limit / no truncation**: the whole file is handed to the dump program every time, same as `more` already reads the whole file directly for text preview. Quitting the pager early is expected to terminate the dump program via the normal broken-pipe signal once the pager stops reading — this PRD does not add any explicit size cap, pre-check, or streaming logic beyond what the pipe itself already provides.
- **Help text**: unchanged. The existing "space: Preview" help-bar segment already covers this — it remains one key that behaves appropriately based on content, exactly as it already does for the text-vs-binary distinction today.
- **Failure surfacing**: unchanged silent-failure convention. If either stage's program is missing or fails to start, the affected child simply exits and the app still reports the same success outcome it already reports for a launched pager/editor — no exit-status inspection is added, consistent with how a missing editor or pager is already (not) handled today.
- **No new `Model` fields, `AppMode`, `Msg`, or `Cmd` variant.** The entire feature is: the existing preview command's binary branch now does a two-process pipe instead of returning a failure, plus one small new piping helper.

## Testing Decisions

- Good tests here assert on pure function return values, never on terminal output, real filesystem state, or by shelling out to external programs — consistent with the testing conventions already established across the roadmap.
- **`is_binary_content`**: no changes, no new tests needed — the existing table-driven coverage from `002-file-preview.md` (empty buffer, all-printable text, a marker byte at start/middle/end, boundary-length buffer) already fully exercises the logic this feature depends on.
- **The preview command's binary branch and the new piping helper** are fork/exec/I-O code and are explicitly **not** unit tested, matching the existing, already-established convention that excludes the text-preview branch and the editor-launch command from unit tests. Validated manually by running the app against both a text file and a binary file and confirming the correct one of "paged as text" / "hex-dumped" happens, that quitting either returns cleanly to the listing, and that quitting a large file's hex view early doesn't hang or leave a lingering process.
- No scripted terminal integration tests for the pager or dump program — same "manual validation only" convention already established for the vim and `more` launches.

## Out of Scope

- Making the dump tool or its output format configurable — hardcoded, same convention as the hardcoded pager choice for text preview.
- Any in-app/panel hex viewer, scrolling logic, or rework of the flat-list output model — explicitly out of scope per the roadmap-wide "no panels" principle already settled in `001-foundation.md` and already applied once to reshape `002-file-preview.md` itself.
- Jump-to-offset, search-within-hex, or byte-level editing — this is a read-only glance, same spirit as text preview.
- An alternate hex dump format (e.g. a different column layout or grouping) — one canonical format only.
- Any size-based truncation, streaming progress indicator, or pre-flight file-size check — explicitly decided against; the pipe's own backpressure and the pager's own quit behavior are considered sufficient.

## Further Notes

Depends on `001-foundation` (Model/Msg/Cmd architecture, `update`/`view` split, no-panels principle) and `002-file-preview` (the preview command, the binary-detection helper, and the fork/exec-a-pager pattern this PRD extends). This PRD is the direct fulfillment of the "future hex view for binary files" pointer `002-file-preview.md` explicitly left open when it extracted binary detection as a standalone, reusable, pure helper rather than inlining it — that forward reference is now resolved, and this PRD also formally retires `002-file-preview.md`'s user story 4 ("binary file rejected with a clear error message"), which no longer reflects the app's behavior once this ships.

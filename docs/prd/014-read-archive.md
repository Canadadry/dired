---
title: "Read archive capabilities"
description: "dired can only browse the real filesystem — finding or extracting a single file inside a tar/zip archive means leaving dired entirely and shelling out to tar/unzip by hand."
status: done
branch: feat/read-archive
---

## Problem Statement

dired has no concept of "inside an archive." A `.zip` or `.tar.gz` is just an
opaque regular file: `MSG_ACTIVATE` on it launches vim on the raw archive
bytes, which is never useful. Finding a single file inside an archive, or
pulling one out to disk, means dropping to `:` Run Command and typing
`tar`/`unzip` invocations by hand — outside dired's normal navigation,
selection, filter/glob, and copy-out model entirely.

## Solution

Entering a `.zip` or supported `.tar.*` file (`→`/Enter, exactly like
entering a directory) browses **into** it: the same arrow-key navigation,
the same `f`/`F` filter and `g`/`G` glob, the same Space-preview and
Enter-to-open, all sourced from the archive's member listing instead of a
real directory's contents. `←`/Left backs out one level at a time — out of
a subfolder, out of the archive itself, and (for an archive found while
already browsing inside another archive) out of the inner archive back into
the outer one — exactly mirroring today's parent-directory navigation.

The path bar shows this uninterrupted, as literal path segments
(`/home/user/downloads/project.zip/src/assets.zip`), so an archive reads as
"just a folder" rather than a distinct mode.

Because this is explicitly a **read** capability, everything that would
write to an archive is blocked with a `MODE_ERROR` ("not possible in
archive"): Rename, Create, Delete/Trash, Paste-into, Yank-Move, and `:` Run
Command. The one exception is copying a single file **out**: yanking a
file-type member (not a directory) and pasting extracts it to the chosen
real-filesystem destination, reusing the existing yank/paste flow. Opening
a file (Enter) or previewing it (Space) both extract it to a private,
read-only tmp copy first, then hand off to the existing vim/`more` paths
unchanged — vim shows it as read-only rather than silently discarding any
edits.

## User Stories

1. As a user, I want to press Enter/→ on a `.zip` or `.tar`/`.tar.gz`/`.tgz`/`.tar.bz2`/`.tbz2`/`.tar.xz`/`.txz`/`.tar.Z` file, so that I browse its contents the same way I'd browse a directory.
2. As a user, I want the path bar to show my location inside an archive as ordinary path segments, so that an archive doesn't visually stand out as a special mode.
3. As a user, I want arrow-key navigation, selection, and scrolling inside an archive listing to work exactly like a real directory listing, so that I don't have to learn a second navigation model.
4. As a user, I want Left/← to back out of an archive subfolder one level at a time, so that going up mirrors real directory navigation exactly.
5. As a user, I want Left/← at an archive's top level to exit back to the real directory (or outer archive) that contains it, so that "backing out" of an archive is just one more parent-navigation step.
6. As a user, I want to enter an archive found *inside* another archive, so that nested archives (a `.zip` inside a `.tar.gz`, etc.) are browsable to any depth without leaving dired.
7. As a user, I want backing out of a nested archive to return me to the exact subfolder of the outer archive I was in before I entered it, so that nested browsing doesn't lose my place.
8. As a user, I want the archive's member listing to be fetched once when I first enter it, so that moving between subfolders inside the same archive is instant and doesn't repeatedly shell out.
9. As a user, I want `f`/`F` filter mode to narrow the current archive-level listing live, exactly like it does for a real directory, so filtering feels consistent everywhere in the app.
10. As a user, I want `g`/`G` glob mode to search recursively across an entire archive's contents (not just the current subfolder), so that finding a deeply-nested file inside an archive works the same way it does on disk.
11. As a user, I want glob/filter results inside an archive capped at the same limit as everywhere else in the app, with a non-blocking status-line hint if there are more matches than shown, so that a huge archive can't hang or interrupt me.
12. As a user, I want Space to preview a file inside an archive (paged text, or hex-dumped if binary), so that I can peek at its contents without fully opening it.
13. As a user, I want Enter on a file-type member to open it in vim, so that I can read (not edit) its contents using the editor I already use for real files.
14. As a user, I want the copy vim/preview opens to be read-only, so that vim visibly warns me before I could accidentally try to save changes that would silently vanish.
15. As a user, I want to yank-copy a single file from inside an archive and paste it into a real directory, so that I can extract exactly the file I need without leaving dired or typing a tar/unzip command myself.
16. As a user, I want the pasted file's name collision handling (auto-renaming on conflict) to work identically whether the yanked source came from a real directory or from inside an archive, so paste behaves consistently everywhere.
17. As a user, I want yank-copy on a directory-type entry inside an archive to be blocked ("not possible in archive"), so that I'm not surprised by a partial or silently-failing whole-subtree extraction.
18. As a user, I want Rename, Create, Delete, Trash, and Paste-into blocked with a clear "not possible in archive" message while browsing inside an archive, so that I understand immediately why the action didn't happen instead of it silently failing or corrupting the archive.
19. As a user, I want Yank-Move blocked the same way, so that I'm not misled into thinking a file was removed from the archive when it wasn't.
20. As a user, I want `:` Run Command blocked the same way while inside an archive, so that I'm not left wondering what `$FILE` or the working directory would even mean in that context.
21. As a user, when I try to enter a `.zip` and `unzip` isn't installed on the system, I want a clear error message rather than the app silently failing or hanging.
22. As a user, when I try to enter a corrupted or password-protected archive, I want a clear error message (whatever the underlying tool reports) and to be returned to where I was, rather than getting stuck in a broken view.
23. As a user, I want sorting (`s`) and directory grouping (`d`) to work on archive listings the same way they do on real directories, so browsing feels consistent.
24. As a user, I want the hidden-files toggle (`a`) to apply to archive member names the same way it applies to real files, so dotfiles inside archives are hidden/shown consistently with the rest of the app.
25. As a developer, I want a pure function that maps a filename to its archive format (or "not an archive"), so that the enter-archive decision is table-driven and unit-testable without touching the filesystem.
26. As a developer, I want the parsing of `tar -tvf`/`unzip -l` output into a structured member list to be a pure function fed canned text, so that listing behavior is unit-testable without forking a real `tar`/`unzip`.
27. As a developer, I want a pure function that computes "the entries visible at this subfolder" from an archive's full cached member list, so that subfolder navigation inside an archive has no filesystem or process dependency and is unit-testable.
28. As a developer, I want the actual forking/piping of `tar`/`unzip` (listing and single-member extraction) kept in the same un-unit-tested, manually-validated category as `load_directory`/`execute_preview`/`walk_glob_matches`, so the testing story stays consistent with the rest of the codebase.
29. As a user, I want any tmp files created to support reading inside an archive cleaned up when I back out of that archive level (or on quit), so that repeated archive browsing doesn't leave clutter behind.

## Implementation Decisions

- **Format detection**: a pure `archive_format_for_name`-style function maps a filename's extension to `ARCHIVE_NONE` / `ARCHIVE_TAR` / `ARCHIVE_ZIP`. Recognized: `.tar`, `.tar.gz`/`.tgz`, `.tar.bz2`/`.tbz2`, `.tar.xz`/`.txz`, `.tar.Z` → tar; `.zip` → zip. `MSG_ACTIVATE` on a regular file with a recognized extension enters archive mode instead of launching the editor; everything else (including bare `.gz`/`.bz2`/`.xz`, `.rar`, `.7z`) is out of scope and opens as a normal file, unchanged.
- **No proactive tool-presence check**: consistent with this app's existing convention (no `which` check anywhere today), `tar`/`unzip` are invoked directly via `execvp`. A missing binary surfaces as an ordinary exec failure, folded into the same `MODE_ERROR` path already used for other failed operations (`msg_failed`-style, capturing stderr where the tool did run).
- **Archive navigation state**: the `Model` gains an explicit, bounded stack of archive levels (nesting has a small hard cap, mirroring the fixed-size-array philosophy already used for `MAX_ENTRIES`/glob's cap) rather than re-deriving "am I inside an archive" by inspecting the path string at execute time. Each level records: which tool it belongs to (tar/zip), the name it should display as a path segment, the real file the tool should read (the archive file itself at the outer level; a tmp-extracted file for a nested level), the subfolder currently being browsed within it, its cached full member listing, and whether its source file is a tmp file that needs deleting when the level is popped. This keeps `update()` fully in charge of deciding what to list/extract from explicit state — `dired.c`'s execute layer never re-probes the filesystem to figure out whether a path crosses an archive boundary.
- **One listing fetch per archive-level entry**: entering an archive (or a nested archive) issues a new `Cmd` that forks `tar -tvf`/`unzip -l` once, parses the full recursive output into the level's cached member list. Navigating between subfolders within that same level is pure in-memory filtering of the cached list (prefix-match on the subfolder path, collapsed into immediate children, inferring implied directories the same way real tar/zip listings sometimes omit explicit directory entries) — no re-shelling until the level itself is re-entered from outside.
- **Path display**: archive segments render as ordinary path components with no distinguishing marker, appended to whatever real or outer-archive path precedes them.
- **Left/parent navigation**: `MSG_GO_PARENT`'s existing handling is extended so that, when inside an archive, it first pops one subfolder segment (mirroring real-directory parent nav); once the subfolder is empty, it pops the whole archive level, restoring the containing level's (or real filesystem's) prior state, and — if the popped level's source was a tmp-extracted file — deletes that tmp file.
- **Entering a nested archive**: since neither `tar` nor `unzip` can read a member out of an archive without a real file to operate on, entering an archive-format member found while already inside an archive first extracts that one member to a tmp file (the same single-member extraction path used for open/preview/copy-out), then pushes a new archive level sourced from that tmp file.
- **Blocked operations**: while any archive level is active, Rename, Create, Delete/Trash (both variants), Paste (pasting *into* the archive), Yank-Move, and `:` Run Command all resolve to `MODE_ERROR("not possible in archive")` instead of their normal `Cmd`. Yank-Copy on a directory-type member resolves the same way (folder copy-out is out of scope). Yank-Copy on a file-type member is the one allowed mutation-adjacent action.
- **Copy-out (yank + paste)**: yanking a file-type archive member records enough of the active archive level's state to identify it (which level/tool, the member's path) instead of a plain real-filesystem path. Paste extracts that single member (via the same extraction mechanism used for open/preview) directly to the resolved destination path in the real directory being pasted into, reusing the existing available-name-collision resolution (`find_available_name`) against that directory's real listing unchanged.
- **Open (Enter) and Preview (Space) on a member**: both first extract the member to a fresh, read-only (chmod'd) tmp file, then hand off to the existing `CMD_LAUNCH_EDITOR`/`CMD_PREVIEW` execution paths unmodified, pointed at that tmp file instead of a real-directory path. The tmp file is disposable — deleted once the editor/pager exits.
- **Filter (`f`/`F`) and glob (`g`/`G`) inside archives**: both modes reuse their existing predicates and interaction model (composing, mutual exclusion, Enter-to-commit for glob per the current inline-walk design) unchanged, but source from the active archive level's cached member list instead of `unfiltered_entries`/a real-filesystem walk. Glob's existing cap-at-`MAX_ENTRIES`-with-status-line-hint behavior (no blocking error) applies identically.
- **Failure handling**: a failed listing or extraction (corrupt archive, password-protected zip, tool exec failure, unsupported compression) surfaces via the existing `MODE_ERROR` screen carrying the tool's stderr, and does not push a broken archive level — you're left exactly where you were before attempting to enter it.
- **Sort/group/hidden-files**: `s`/`d` cycling and the `a` hidden-files toggle apply to archive listings the same way they apply to real directories, using whatever fields the tool's output provides (name and size always; modification time where `tar -tvf`/`unzip -l` report it) — synthetic entries without real permission bits (zip in particular, which reports no permission info) get a placeholder in place of a real mode string rather than fabricated permissions.
- **Tmp file lifecycle**: extraction tmp files live under a per-session tmp location, cleaned up as their owning level is popped (nested-archive source files) or as their single-use editor/preview copy is consumed; any leftovers get a best-effort cleanup on quit.

## Testing Decisions

Consistent with every prior PRD's Testing Decisions: good tests here assert on pure function outputs and on `update()`'s returned `Model`/`Cmd`, never on terminal rendering, real filesystem state, or by forking a real `tar`/`unzip`.

- **`archive_format_for_name`**: table-driven — every recognized extension (including the multi-part `.tar.gz`/`.tar.bz2`/`.tar.xz` forms), a `.zip`, unrecognized extensions, `.rar`/`.7z`/bare `.gz` (all `ARCHIVE_NONE`), and a name with no extension at all.
- **`parse_tar_listing` / `parse_zip_listing`**: table-driven, fed canned `tar -tvf`/`unzip -l` stdout text — files at multiple depths, an explicit directory entry, a tree relying on implied (not explicitly listed) directories, an empty listing, and a listing with an unusual/edge-case name (spaces, a leading `./`).
- **The "children at this level" function**: table-driven against canned member lists — root level, a nested subfolder, a subfolder with only implied (not explicit) directory entries, and an empty result for a subfolder with no members.
- **`update()`-level tests** (same harness as `filter`/`glob`'s existing suites in `update_test.c`): entering an archive-format file from nav mode produces the listing `Cmd`; `MSG_GO_PARENT` inside a subfolder pops one segment without leaving the archive; `MSG_GO_PARENT` at an archive's root pops the whole level (and, for a nested level, includes the tmp-cleanup intent); each blocked operation (rename/create/delete/trash/paste-into/yank-move/run-command) while inside an archive produces `MODE_ERROR` with the "not possible in archive" message and no other side effect; yank-copy on a directory-type member is blocked the same way; yank-copy on a file-type member followed by paste in a real directory produces the extraction `Cmd` with the resolved, collision-free destination name; filter/glob committed while inside an archive operate against the level's cached list rather than issuing any real-filesystem `Cmd`.
- **Listing/extraction execution itself is not unit-tested**, matching the established convention for `load_directory`/`execute_preview`/`walk_glob_matches` (real fork/exec/pipe I/O, no canned-buffer seam to test against at that layer — the parsing seam above is where the coverage lives instead). Validated manually against: a small tar and a small zip archive; a nested archive inside another archive; a corrupted archive; a password-protected zip; an archive exceeding the glob/filter display cap; opening a member in vim and confirming it's read-only; copy-out via yank/paste landing correctly in a real directory with a name collision.

## Implementation Chunks

This PRD is too wide to build as one vertical slice, so it's cut into sequential,
independently-testable chunks. Each chunk builds only on prior chunks, follows
the tdd skill's tracer-bullet loop on its own, and should reach a green test
suite before the next chunk starts. Story/decision references are to the
sections above.

- [x] **Chunk 0 — `feat/read-archive`, commit `b0c8fe8`**: `archive_format_for_name`
  pure classifier (story 25), table-tested. `MSG_ACTIVATE` on a regular file
  with a recognized extension returns `CMD_LIST_ARCHIVE` (carrying the detected
  `ArchiveFormat`) instead of `CMD_LAUNCH_EDITOR` — routing only, no listing
  execution wired up yet (`CMD_LIST_ARCHIVE` is currently a no-op in `dired.c`).
- [x] **Chunk 1 — commit `f9d25d6`**: `parse_tar_listing` / `parse_zip_listing` pure functions
  fed canned `tar -tvf`/`unzip -l` stdout text (story 26), and the pure
  "children visible at this subfolder" function over a cached member list
  (story 27). No `Model`/`update()` wiring yet — parsing/derivation only.
- [x] **Chunk 2 — commit `b5d6920`**: `CMD_LIST_ARCHIVE`'s real
  fork/exec of `tar -tvf`/`unzip -l` (story 28, joins the un-unit-tested
  execution layer alongside `load_directory`/`execute_preview`); `Model` gains
  the bounded archive-level stack; a new `Msg` (e.g. `MSG_ARCHIVE_LISTED`)
  feeds the parsed listing into `update()`, pushing the first level and
  populating `entries` via chunk 1's children-at-root-subfolder function
  (completes story 1, story 8's one-fetch-per-level). Path bar renders the
  archive as an ordinary path segment (story 2).
- [x] **Chunk 3 — commit `77c2130`**: `MSG_ACTIVATE` on a
  directory-type member descends a subfolder in place (pure re-filter of the
  cached list, no re-fetch); `MSG_GO_PARENT` pops one subfolder segment, then
  pops the whole level back to the containing level or real filesystem once
  the subfolder is empty (stories 3, 4, 5).
- [x] **Chunk 4 — commit `c50554d`**: entering an archive-format member while
  already inside a level extracts it to a tmp file first, then pushes a new
  level sourced from that tmp file; popping a tmp-sourced level deletes the
  tmp file (stories 6, 7, and the nested-level part of story 29).
- [x] **Chunk 5 — commit `594fa11`**: `f`/`F` and `g`/`G` reuse their
  existing predicates and interaction model unchanged, but source from the
  active level's cached member list instead of `unfiltered_entries`/a real
  walk; same cap-with-status-line-hint behavior (stories 9, 10, 11).
- [x] **Chunk 6 — commit `dd7d55d`**: Enter and Space extract the member to a
  fresh, read-only tmp file, then hand off unmodified to the existing
  `CMD_LAUNCH_EDITOR`/`CMD_PREVIEW` paths (stories 12, 13, 14).
- [x] **Chunk 7 — commit `a70024a`**: yank-copy on a file-type member records the level
  and member path instead of a real filesystem path; yank-copy on a
  directory-type member is blocked; paste extracts to the resolved,
  collision-free destination in the real directory (stories 15, 16, 17).
- [x] **Chunk 8 — commit `9246af2`**: Rename, Create, Delete/Trash,
  Paste-into, Yank-Move, and `:` Run Command all resolve to
  `MODE_ERROR("not possible in archive")` while any archive level is active
  (stories 18, 19, 20).
- [x] **Chunk 9 — commit `e46a699`**: a missing `tar`/`unzip` binary, a corrupted
  archive, and a password-protected archive all surface via the existing
  `MODE_ERROR` path carrying the tool's stderr, leaving no broken level
  pushed (stories 21, 22).
- [x] **Chunk 10 — commit `8d9898c`**: `s`/`d` cycling
  and the `a` hidden-files toggle apply to archive listings, including the
  placeholder mode string for zip's missing permission bits; any remaining
  leftover tmp files get a best-effort cleanup on quit (stories 23, 24, and
  the remainder of story 29).

## Out of Scope

- **`.rar`, `.7z`, and bare single-file `.gz`/`.bz2`/`.xz`** (compression without a container) — not recognized as enterable archives.
- **Any write path into an archive** — rename, create, delete, move, or paste-into are all blocked, not implemented in a limited form.
- **Copying a directory-type entry out of an archive** — only single-file copy-out is supported; folder extraction is dropped from this PRD.
- **`:` Run Command inside an archive**, in any form (including against an extracted tmp copy) — blocked outright.
- **Password-prompt UI for encrypted/protected archives** — a protected archive just fails with whatever error the underlying tool reports.
- **A proactive `tar`/`unzip` availability check** — failures surface reactively through a failed `execvp`, matching this app's existing convention everywhere else.
- **Fully extracting an archive to disk on entry ("unarchive" as a bulk operation)** — flagged by the developer as a plausible future feature, explicitly not part of this PRD, which stays lazy/listing-based throughout.
- **Configurable or higher-than-default caps on archive listing/glob/filter results** — reuses whatever cap the rest of the app already uses, with no archive-specific tuning.

## Further Notes

Depends on `001-foundation.md` for the core `Model`/`Msg`/`Cmd`/`update()` loop, and directly reuses machinery from `010-filter-mode.md` (`filter_matches`/`filter_is_valid`/composing-mode interaction) and `013-glob-inline-recursive-walk.md` (glob's commit-on-Enter, cap-with-status-line-hint behavior, no blocking-error truncation) unchanged — this PRD only changes *where* filter/glob source their candidate list from when an archive level is active, not how either mode behaves once it has one.

The developer explicitly flagged eager "extract the whole archive to disk on entry" as a simpler alternative that was considered and rejected in favor of the lazy/listing approach described here, but noted it as a plausible separate future feature (full-tree extraction on demand) — worth keeping in mind if a later PRD revisits this area, but not something this PRD should anticipate structurally.

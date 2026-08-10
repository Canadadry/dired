---
title: "Async action queue + progress bar"
description: "Once bulk or slow operations exist (copying many files, trashing a multi-select), running them synchronously would freeze the UI for the duration."
status: needs-triage
---

> **Draft status:** written from the high-level architecture/roadmap grilling session only. The overall architecture was discussed in reasonable depth, but the concrete protocol/edge cases below are undecided. Needs a focused grilling session before it's implementation-ready.

## Problem Statement

The foundation PRD's `Cmd` mechanism is deliberately synchronous — `main()`
executes one effect immediately and blocks until it's done. That's fine for
single, fast operations (rename, single-file copy, single delete), but once
bulk operations exist (multi-select delete/copy/move, per `11-multi-select`),
running many of them one after another synchronously would freeze the whole
UI for the duration, with no feedback on progress.

## Solution

Introduce a queue of actions that can be dequeued and executed one at a
time via `fork`+`exec`, outside the main event loop, reporting progress back
without blocking keyboard input. A progress bar shows how many of the total
queued actions have completed.

## User Stories

1. As a user, I want to see a progress indicator while a batch of file operations runs, so that I know the app hasn't frozen.
2. As a user, I want to keep using the app (e.g. see the UI redraw, and eventually cancel) while a batch operation is running, so that a long operation doesn't lock me out.
3. As a developer, I want a mechanism to run queued actions without busy-waiting or blocking the keyboard-input wait, so that the terminal stays responsive.
4. As a developer, I want `03-copy-move` and `08-trash`'s synchronous `Cmd`s to be upgradeable to run through this queue for bulk cases, without having to redesign their effect logic.

## Implementation Decisions

**Decided:**
- Two-process-oriented design: the main process keeps running the `update`/`view`/input loop; queued actions are executed by a separate process (or one child process per queued action, spawned via `fork`+`exec`) rather than inline in the main process.
- Progress is reported back to the main process over a pipe. The read end of that pipe's file descriptor is folded into the main process's existing wait — using termbox2's `tb_get_fds(ttyfd, resizefd)` alongside the pipe fd in one `select()`/`poll()` call — so waiting for progress updates never becomes a busy-loop and never requires a second, separate blocking read loop.
- Actions are dequeued one at a time and executed via `exec`; the progress bar reflects **count of completed vs. total queued actions**, not a per-action percentage (explicitly not "37% through this one file" — "un progress bar sur le nombre d'action total").
- This queue is what `03-copy-move` and `08-trash` (and any bulk operation introduced by `11-multi-select`) get upgraded to use for multi-action or slow cases; single, fast operations can plausibly stay on the synchronous `Cmd` path from `01-foundation` — *whether that split (sync for single ops, queued for bulk) is the actual intended design, or whether the queue replaces the synchronous path entirely, needs to be confirmed in dedicated grilling.*

**Not yet decided — needs dedicated grilling:**
- The exact IPC protocol/message framing over the pipe (what bytes actually get written to report "one action completed" or "an action failed").
- Whether it's literally one long-lived second process that runs the whole queue, or one short-lived child process per queued action (the roadmap discussion mentioned both phrasings without settling on one).
- Cancellation: can a running queue be aborted, and how (kill the child process(es), and what happens to a partially-completed action)?
- Per-action failure handling: does one failed action stop the whole queue, skip and continue, or something else? How are per-action errors surfaced to the user?
- How queued actions are constructed from `update()` — does `Cmd` gain a "batch" variant, or is the queue a wholly separate mechanism sitting beside `Cmd`?
- Whether the queue process needs its own crash-recovery story (what happens to the UI if the action-runner process dies mid-batch).

## Testing Decisions

- Not yet decided. The queueing/dequeueing logic (given a list of pending actions and a "one completed" event, what's the next state) is a plausible table-driven test target once its shape is settled; the actual `fork`/`exec`/pipe mechanics are I/O/process management and not unit-testable in the same way.

## Out of Scope

- The specific bulk operations that will use this queue (those are `03-copy-move`'s and `08-trash`'s concern, and `11-multi-select`'s) — this PRD is the queue mechanism itself.

## Further Notes

Depends on `01-foundation` (for the `Cmd` shape it extends/complements) and conceptually pairs with `11-multi-select`, which is the main consumer of bulk queued actions — sequenced second-to-last deliberately, once there's an actual need for it.

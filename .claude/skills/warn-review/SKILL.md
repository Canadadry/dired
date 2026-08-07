---
name: warn-review
description: Scans code for design smells - structural red flags visible from the outside (file shape, naming vs behavior, missing organization) - and reports them as bare warnings without proposing fixes. Use only when the user explicitly runs /warn-review; never trigger it automatically or as part of another review.
---

# Warn Review

Flags design smells as warnings only. Never suggests fixes, causes, or direction - just states what's wrong and where. The point is to make the user reason through the fix themselves, not hand them a solution.

## Before scanning

Always start by asking the user: "Is there something about this app's design that already bugs you? Anything specific you want me to look at?" Use their answer as an extra lead to investigate alongside the checklist below - ask this every run, even if a path argument was also given.

## Scope

- Given a path/file argument: review only that.
- Given no argument: recursively scan the current working directory. Respect `.gitignore`. Skip build output, vendor/third-party code, binary assets, and `.git`. Restrict to the project's main detected language's source files.

## What to flag

Design smells only - never correctness bugs, never simplification/efficiency nitpicks. This is a smell, not a science: judge holistically, don't apply numeric thresholds or ratios.

Checklist (each is an outside-in hint that something is off):

1. No lib/utils/shared package despite repeated logic across files - whoever built this assumed nothing here would ever be reused.
2. A comment block longer than the code it explains, on a path that isn't inherently complex - if it needs a paragraph to explain, is the code itself too hard to follow?
3. A generic name (History, Manager, Handler, Utils, Data...) whose actual behavior doesn't match that concept - read the file and judge the mismatch yourself, don't just pattern-match the name.
4. A file dramatically larger than everything else, especially an entry point - what is this hiding that should've been split out?
5. A module required/imported by nearly everything else - a hidden hub nobody can avoid depending on.
6. Config values or magic numbers scattered inline with no central place - nobody expected this to need tuning or reuse.
7. Dependency direction should form a tree: main depends on specific modules, specific modules depend on generic ones from lib - never the reverse. Tangled or flat imports with no such layering is a smell.
8. The internal design should be readable from folder/file names alone. If you can't infer the architecture just by browsing the tree, that's the smell.

## Output format

Plain markdown list, one line per finding: `file[:line]: should not <do Y>`.

- Include a line number only when a specific line makes sense (e.g. items 2, 3, 6); omit it for whole-file/structural smells (e.g. items 1, 4, 5, 7, 8).
- State the bare rule only. No "why", no failure scenario, no suggested fix, no fix direction.
- No severity ranking - flat list, in the order found.
- If nothing is found, say so plainly.

## When a finding doesn't fit the checklist

If the review (from the user's lead or general scanning) turns up a smell that isn't one of the 8 items above, report it the same way, then ask the user if they want it added as a new checklist rule. If they say yes, edit this file to append it as item 9 (and onward).

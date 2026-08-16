# Feedback: `dontAsk` permission mode executes commands not covered by allow list, static read-only list, or hook approval

To file at https://github.com/anthropics/claude-code/issues (or via `/feedback` in a session).

## Description

With `permissions.defaultMode: "dontAsk"` set, docs state (permission-modes.md):

> Claude Code auto-denies every tool call that would otherwise prompt you. Claude runs only actions matching your `permissions.allow` rules, read-only Bash commands, and calls approved by a PreToolUse hook.

Observed: `python3 -c 'print("Hello, world!")'` executed without a prompt and without denial, despite:

- No `Bash(python3*)` (or similar) entry in `permissions.allow`.
- `python3` not being part of the documented static read-only command list (`ls, cat, echo, pwd, head, tail, grep, find, wc, which, diff, stat, du, cd`, read-only `git`).
- The repo's two `PreToolUse` hooks (`block-git-c.py`, `deny-absolute-paths.py`) only ever emit `deny` decisions on specific patterns; neither matched this command, so neither issued an `allow`.

Per docs this command should have been auto-denied. Instead it ran. This is concerning specifically because `python3 -c <code>` is a fully general escape hatch — if arbitrary interpreter invocations bypass `dontAsk`'s allow-list gate, that undermines the mode's intended containment.

## Config used

```json
"permissions": { "allow": [ ... ], "defaultMode": "dontAsk" }
```

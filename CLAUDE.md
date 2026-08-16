never run dired directly you cannot control a tui app

## Build and test

- `make unit` — builds the debug binaries from a clean state and runs the C
  unit test suite (equivalent to `./builder clean debug test`).
- `make test` — runs `make unit`, then drives the real `diredd` binary under
  a pseudo-terminal for the integration suite in `test/integration/`.

## Permission denials

Before touching a file, check `.claude/settings.json`'s `permissions.allow`
list to see what you actually have access to. If a Write/Edit hits a
permission denial, judge whether the task truly requires touching that
file. If it doesn't, drop that part of the task rather than working around
the denial via Bash or any other mechanism. If it genuinely does, stop and
report the exact tool, path, and error to the user instead of routing
around it.

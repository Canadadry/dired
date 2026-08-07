#!/usr/bin/env python3
"""
PreToolUse hook: rejects `git -C <path> ...` commands.
Forces Claude to work in the current repo (cwd) rather than
pointing at another path out of excessive caution.
"""
import json
import re
import sys

# Matches `git -C <something>` in its common forms:
#   git -C /path/to/repo status
#   git -C ../foo commit -m "x"
#   git --git-dir=... --work-tree=... (equivalent variant to also watch for)
GIT_DASH_C = re.compile(r"\bgit\s+(?:\S+\s+)*-C\s+\S+")
GIT_DIR_FLAGS = re.compile(r"\bgit\s+(?:\S+\s+)*--(git-dir|work-tree)=\S+")


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if payload.get("tool_name") != "Bash":
        return 0

    command = payload.get("tool_input", {}).get("command", "")

    if GIT_DASH_C.search(command) or GIT_DIR_FLAGS.search(command):
        print(
            "BLOCKED: do not use `git -C <path>` or --git-dir/--work-tree.\n"
            "Work directly in the current directory: just rerun the git "
            "command without the -C option (the cwd is already the "
            "correct repo).",
            file=sys.stderr,
        )
        return 2

    return 0


if __name__ == "__main__":
    sys.exit(main())

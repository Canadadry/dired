#!/usr/bin/env python3
"""commit-msg hook: refuse commits whose message credits a co-author.

Matches "co-author" in any spacing/hyphenation/case, anywhere in the
message, so it catches trailers like:
  Co-authored-by: Name <email>
  Co-Authored-By: Name <email>
  Co-author: Name
  coauthored by Name
  Co Authors: Name
"""
import re
import sys

PATTERN = re.compile(r"co[\s_-]*author", re.IGNORECASE)


def main():
    path = sys.argv[1]
    with open(path, "r", encoding="utf-8") as f:
        message = f.read()

    if PATTERN.search(message):
        print("commit-msg: refusing commit that credits a co-author", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Pre-commit hook: strip disposable comments from newly added lines.

Enforces the rule in the git-commit skill - a commit's newly added lines
keep `// TODO` / `/* TODO ... */` comments but lose every other `//` or
`/* ... */` comment. Runs against the staged diff so it applies to every
commit, not just ones made through the git-commit skill.

Tracks a real lexer state (code / string / line-comment / block-comment)
instead of only looking for `//`, so a `//` that appears inside a
`/* ... */` block (e.g. a URL in a comment) is not mistaken for the start
of a line comment, and multi-line `/* ... */` blocks are stripped as a
whole rather than left half-eaten.

Installed via scripts/git-hooks/install.sh -> .git/hooks/pre-commit.
"""
import re
import subprocess
import sys

COMMENT_EXTENSIONS = (".c", ".h")


def run(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def added_line_numbers(path):
    """Line numbers (1-indexed, in the new file) added by the staged diff for `path`."""
    diff = run("git", "diff", "--cached", "-U0", "--", path)
    lines = set()
    cur = None
    for line in diff.splitlines():
        if line.startswith("@@"):
            m = re.search(r"\+(\d+)", line)
            cur = int(m.group(1))
        elif line.startswith("+") and not line.startswith("+++"):
            lines.add(cur)
            cur += 1
    return lines


def find_comments(text):
    """Scan `text`, returning every // and /* */ comment as a dict with
    1-indexed start_line/end_line and 0-indexed start_col/end_col (columns
    are relative to each line's content, excluding the newline)."""
    comments = []
    n = len(text)
    i = 0
    line, col = 1, 0
    in_string = None
    while i < n:
        ch = text[i]
        if ch == "\n":
            line += 1
            col = 0
            i += 1
            continue
        if in_string:
            if ch == "\\":
                i += 2
                col += 2
                continue
            if ch == in_string:
                in_string = None
            i += 1
            col += 1
            continue
        if ch in ("'", '"'):
            in_string = ch
            i += 1
            col += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            start_line, start_col = line, col
            j = i
            while j < n and text[j] != "\n":
                j += 1
            comments.append({
                "kind": "line",
                "start_line": start_line, "start_col": start_col,
                "end_line": start_line, "end_col": start_col + (j - i),
                "text": text[i:j],
            })
            col += (j - i)
            i = j
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            start_line, start_col = line, col
            j = i + 2
            jl, jc = line, col + 2
            while j < n:
                if text[j] == "\n":
                    jl += 1
                    jc = 0
                    j += 1
                    continue
                if text[j] == "*" and j + 1 < n and text[j + 1] == "/":
                    j += 2
                    jc += 2
                    break
                jc += 1
                j += 1
            comments.append({
                "kind": "block",
                "start_line": start_line, "start_col": start_col,
                "end_line": jl, "end_col": jc,
                "text": text[i:j],
            })
            line, col, i = jl, jc, j
            continue
        i += 1
        col += 1
    return comments


def is_todo(comment):
    marker = "//" if comment["kind"] == "line" else "/*"
    body = comment["text"][len(marker):].lstrip(" \t\n*")
    return body.lower().startswith("todo")


def eligible(comment, targets):
    span = range(comment["start_line"], comment["end_line"] + 1)
    if not all(ln in targets for ln in span):
        return False
    return not is_todo(comment)


def strip_comments(text, targets):
    comments = [c for c in find_comments(text) if eligible(c, targets)]
    if not comments:
        return text, False

    lines = text.splitlines(keepends=True)
    # segments[line_no] = list of (start_col, end_col) to delete from that line's content
    segments = {}
    full_delete = set()

    def add_segment(ln, start, end):
        segments.setdefault(ln, []).append((start, end))

    for c in comments:
        sl, sc, el, ec = c["start_line"], c["start_col"], c["end_line"], c["end_col"]
        if sl == el:
            add_segment(sl, sc, ec)
        else:
            add_segment(sl, sc, None)  # to end of line content
            for ln in range(sl + 1, el):
                full_delete.add(ln)
            add_segment(el, 0, ec)

    out = []
    for idx, raw in enumerate(lines, start=1):
        if idx in full_delete:
            continue
        if idx not in segments:
            out.append(raw)
            continue
        has_nl = raw.endswith("\n")
        body = raw[:-1] if has_nl else raw
        orig_len = len(body)
        reaches_eol = any(end is None or end == orig_len for _, end in segments[idx])
        for start, end in sorted(segments[idx], key=lambda s: s[0], reverse=True):
            body = body[:start] + body[end if end is not None else len(body):]
        if reaches_eol:
            body = body.rstrip()
        if body.strip() == "":
            continue
        out.append(body + ("\n" if has_nl else ""))

    return "".join(out), True


def process_file(path):
    targets = added_line_numbers(path)
    if not targets:
        return False

    with open(path, "r", encoding="utf-8") as f:
        original = f.read()

    new_text, changed = strip_comments(original, targets)
    if changed and new_text != original:
        with open(path, "w", encoding="utf-8") as f:
            f.write(new_text)
        run("git", "add", path)
        return True
    return False


def main():
    staged = run(
        "git", "diff", "--cached", "--name-only", "--diff-filter=ACM"
    ).splitlines()
    any_changed = False
    for path in staged:
        if not path.endswith(COMMENT_EXTENSIONS):
            continue
        if process_file(path):
            any_changed = True
    if any_changed:
        print(
            "pre-commit: stripped disposable comments from newly added lines",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())

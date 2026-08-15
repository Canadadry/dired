import re
from collections import namedtuple

COLORS = ("red", "green", "yellow", "blue", "magenta", "white", "black")

Cell = namedtuple("Cell", ["char", "fg", "bg", "bright"])

_TOKEN_RE = re.compile(r"</?[a-zA-Z0-9-]+>|[^<]+")


def format_row(row):
    out = []
    i = 0
    n = len(row)
    while i < n:
        fg, bg, bright = row[i].fg, row[i].bg, row[i].bright
        assert fg is None or fg in COLORS
        assert bg is None or bg in COLORS
        j = i
        chars = []
        while j < n and row[j].fg == fg and row[j].bg == bg and row[j].bright == bright:
            chars.append(row[j].char)
            j += 1
        text = "".join(chars)
        if fg is not None:
            text = f"<{fg}>{text}</{fg}>"
        if bright:
            text = f"<bright>{text}</bright>"
        if bg is not None:
            text = f"<bg-{bg}>{text}</bg-{bg}>"
        out.append(text)
        i = j
    return "".join(out)


def format_screen(screen):
    return [format_row(row) for row in screen]


def _parse_row(s):
    cells = []
    fg_stack = [None]
    bg_stack = [None]
    bright_stack = [False]
    for token in _TOKEN_RE.findall(s):
        if token.startswith("</"):
            name = token[2:-1]
            if name == "bright":
                bright_stack.pop()
            elif name.startswith("bg-"):
                bg_stack.pop()
            else:
                fg_stack.pop()
        elif token.startswith("<"):
            name = token[1:-1]
            if name == "bright":
                bright_stack.append(True)
            elif name.startswith("bg-"):
                color = name[3:]
                assert color in COLORS
                bg_stack.append(color)
            else:
                assert name in COLORS
                fg_stack.append(name)
        else:
            for ch in token:
                cells.append(Cell(ch, fg_stack[-1], bg_stack[-1], bright_stack[-1]))
    return cells


def parse(tagged):
    if isinstance(tagged, str):
        return _parse_row(tagged)
    return [_parse_row(row) for row in tagged]

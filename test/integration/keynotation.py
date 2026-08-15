_SPECIAL_KEYS = {
    "<Enter>": b"\x0d",
    "<Esc>": b"\x1b",
    "<Up>": b"\x1bOA",
    "<Down>": b"\x1bOB",
    "<Right>": b"\x1bOC",
    "<Left>": b"\x1bOD",
    "<BS>": b"\x7f",
    "<Del>": b"\x1b[3~",
}


def encode_keys(keys):
    out = bytearray()
    for key in keys:
        if key in _SPECIAL_KEYS:
            out += _SPECIAL_KEYS[key]
        else:
            out += key.encode("utf-8")
    return bytes(out)

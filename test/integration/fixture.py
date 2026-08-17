import os
import struct
import subprocess
import tempfile

ALLOWED_GIT_SUBCOMMANDS = {"add", "commit"}

# Mirrors src/history.h/src/history.c: two fixed-size arenas (folder/file
# history) live right after a 12-byte header (3 x uint32_t, no padding) in
# ~/.config/dired_history. In BUILD_DEBUG (how the integration binary is
# built), dired_effective_home() resolves "~" to the process's cwd, i.e. the
# fixture root - so pre-writing this file before launching diredd lets a
# test start with folder/file history already populated, without needing to
# drive a real preview/edit action through the pty (which forks an
# interactive editor/pager the harness's per-keystroke render-sync protocol
# cannot complete - see smoke_*_picker_enter_and_space_on_empty_is_noop.json
# for why that path is untestable here).
HISTORY_FILE_MAGIC = 0x54534968
HISTORY_FILE_VERSION = 2
FOLDER_HISTORY_ARENA_BYTES = 20480
FILE_HISTORY_ARENA_BYTES = 40960


def _build_history_arena(size, entries):
    """Replicates history_arena_push()'s on-disk layout: entries as
    NUL-terminated text packed from offset 0 forward, paired with a
    little-endian uint16 offset table packed from the end backward (oldest
    entry closest to the tail), and a trailing uint16 count. Entries must be
    given oldest-first, matching push() call order - the picker itself (and
    history_arena_command_at) then reads them most-recent-first."""
    buf = bytearray(size)
    text_end = 0
    count = 0
    for entry in entries:
        data = entry.encode("utf-8") + b"\x00"
        index_top = size - 2 - (count + 1) * 2
        if text_end + len(data) > index_top:
            raise ValueError("history fixture entry does not fit in the arena")
        buf[text_end:text_end + len(data)] = data
        struct.pack_into("<H", buf, index_top, text_end)
        text_end += len(data)
        count += 1
    struct.pack_into("<H", buf, size - 2, count)
    return bytes(buf)


def write_history(fixture_root, resolved_root, history_spec):
    folder_entries = [
        os.path.join(resolved_root, relpath) for relpath in history_spec.get("folder_history", [])
    ]
    file_entries = [
        os.path.join(resolved_root, relpath) for relpath in history_spec.get("file_history", [])
    ]
    if not folder_entries and not file_entries:
        return

    config_dir = os.path.join(fixture_root, ".config")
    os.makedirs(config_dir, exist_ok=True)

    header = struct.pack("<III", HISTORY_FILE_MAGIC, HISTORY_FILE_VERSION, 0)
    folder_arena = _build_history_arena(FOLDER_HISTORY_ARENA_BYTES, folder_entries)
    file_arena = _build_history_arena(FILE_HISTORY_ARENA_BYTES, file_entries)

    with open(os.path.join(config_dir, "dired_history"), "wb") as f:
        f.write(header)
        f.write(folder_arena)
        f.write(file_arena)


def build_fixture(spec):
    setup = spec.get("setup", [])
    for entry in setup:
        if not entry or entry[0] not in ALLOWED_GIT_SUBCOMMANDS:
            raise ValueError("disallowed git setup entry: {!r}".format(entry))

    root = tempfile.mkdtemp()

    for path, file_content in spec.get("tree", {}).items():
        full_path = os.path.join(root, path)
        dirname = os.path.dirname(full_path)
        if dirname:
            os.makedirs(dirname, exist_ok=True)
        with open(full_path, "w") as f:
            f.write(file_content)

    if setup:
        subprocess.run(["git", "init"], cwd=root, check=True, capture_output=True)
        subprocess.run(["git", "config", "user.email", "test@test"], cwd=root, check=True, capture_output=True)
        subprocess.run(["git", "config", "user.name", "test"], cwd=root, check=True, capture_output=True)
        for entry in setup:
            subprocess.run(["git"] + entry, cwd=root, check=True, capture_output=True)

    return root

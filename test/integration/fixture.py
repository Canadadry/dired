import os
import shutil
import struct
import subprocess
import tempfile

ALLOWED_GIT_SUBCOMMANDS = {"add", "commit"}

# Ordered longest-suffix-first so "archive.tar.gz" resolves to "tar.gz" and
# not "gz" (which isn't a format we build). Mirrors the three creatable
# formats from the :zip/:tar/:tar.gz commands (archive-create-extract-
# commands.md); .tgz is accepted too since the *reading* side already
# recognizes it (014-read-archive.md's format classifier), even though it
# isn't one of the create commands' own literal output names.
ARCHIVE_FORMAT_BY_SUFFIX = [
    (".tar.gz", "tar.gz"),
    (".tgz", "tar.gz"),
    (".tar", "tar"),
    (".zip", "zip"),
]

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


def _write_tree(base_dir, tree):
    """Materializes a flat {relpath: text_content} spec as real files under
    base_dir, creating parent directories as needed. Shared by the
    top-level fixture tree and by archive-member staging below, so both
    use the exact same spec shape."""
    for path, file_content in tree.items():
        full_path = os.path.join(base_dir, path)
        dirname = os.path.dirname(full_path)
        if dirname:
            os.makedirs(dirname, exist_ok=True)
        with open(full_path, "w") as f:
            f.write(file_content)


def _archive_format_for(dest_path):
    for suffix, fmt in ARCHIVE_FORMAT_BY_SUFFIX:
        if dest_path.endswith(suffix):
            return fmt
    raise ValueError("cannot infer archive format from fixture archive path: {!r}".format(dest_path))


def _build_archive(dest_path, member_tree):
    """Materializes member_tree (same {relpath: text_content} shape as the
    top-level "tree" spec) into a scratch staging directory, then shells
    out to the real zip/tar binary to pack it into dest_path - so the
    resulting file is byte-for-byte what a real user's zip/tar would
    produce, not hand-crafted archive bytes."""
    fmt = _archive_format_for(dest_path)

    staging_dir = tempfile.mkdtemp()
    try:
        _write_tree(staging_dir, member_tree)
        entries = sorted(os.listdir(staging_dir))
        if not entries:
            raise ValueError("archive fixture {!r} has no members".format(dest_path))

        if fmt == "zip":
            argv = ["zip", "-r", dest_path] + entries
        elif fmt == "tar":
            argv = ["tar", "-cf", dest_path] + entries
        elif fmt == "tar.gz":
            argv = ["tar", "-czf", dest_path] + entries
        else:
            raise ValueError("unsupported archive format: {!r}".format(fmt))

        subprocess.run(argv, cwd=staging_dir, check=True, capture_output=True)
    finally:
        shutil.rmtree(staging_dir, ignore_errors=True)


def build_fixture(spec):
    setup = spec.get("setup", [])
    for entry in setup:
        if not entry or entry[0] not in ALLOWED_GIT_SUBCOMMANDS:
            raise ValueError("disallowed git setup entry: {!r}".format(entry))

    root = tempfile.mkdtemp()

    _write_tree(root, spec.get("tree", {}))

    for path, member_tree in spec.get("archives", {}).items():
        dest_path = os.path.join(root, path)
        dirname = os.path.dirname(dest_path)
        if dirname:
            os.makedirs(dirname, exist_ok=True)
        _build_archive(dest_path, member_tree)

    if setup:
        subprocess.run(["git", "init"], cwd=root, check=True, capture_output=True)
        subprocess.run(["git", "config", "user.email", "test@test"], cwd=root, check=True, capture_output=True)
        subprocess.run(["git", "config", "user.name", "test"], cwd=root, check=True, capture_output=True)
        for entry in setup:
            subprocess.run(["git"] + entry, cwd=root, check=True, capture_output=True)

    return root

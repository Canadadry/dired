import os
import subprocess
import tempfile

ALLOWED_GIT_SUBCOMMANDS = {"add", "commit"}


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

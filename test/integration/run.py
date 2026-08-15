import glob
import json
import os
import shutil
import sys

import fixture
import ptysession
import taggedscreen


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.dirname(os.path.dirname(here))


def diredd_path():
    return os.path.join(repo_root(), "build", "diredd")


def discover_tests():
    here = os.path.dirname(os.path.abspath(__file__))
    return sorted(glob.glob(os.path.join(here, "*.json")))


def run_step(session, step, index, failures, fixture_root):
    keys = step.get("keys", [])
    if keys:
        session.send_keys(keys)

    expect = step.get("expect")
    if expect is None:
        return

    expect = [row.replace("{fixture_root}", fixture_root) for row in expect]

    actual_rows = taggedscreen.format_screen(session.capture())
    if len(actual_rows) != len(expect):
        failures.append(
            "step {}: expected {} rows, got {}".format(index, len(expect), len(actual_rows))
        )
        return

    for row_index, (expected_row, actual_row) in enumerate(zip(expect, actual_rows)):
        if expected_row != actual_row:
            failures.append(
                "step {} row {}:\n  expected: {}\n  actual:   {}".format(
                    index, row_index, expected_row, actual_row
                )
            )


def apply_mutations(fixture_root, mutate):
    for relpath, content in mutate.items():
        with open(os.path.join(fixture_root, relpath), "w") as f:
            f.write(content)


def run_test_file(path, diredd):
    with open(path) as f:
        spec = json.load(f)

    fixture_root = fixture.build_fixture(spec["fixture"])
    apply_mutations(fixture_root, spec["fixture"].get("mutate", {}))
    resolved_root = os.path.realpath(fixture_root)
    session = None
    failures = []
    try:
        session = ptysession.PtySession(diredd, fixture_root)
        for index, step in enumerate(spec["steps"]):
            try:
                run_step(session, step, index, failures, resolved_root)
            except (ptysession.DiredTimeout, ptysession.DiredCrashed) as e:
                failures.append("step {}: {}".format(index, e))
                break
    except (ptysession.DiredTimeout, ptysession.DiredCrashed) as e:
        failures.append(str(e))
    finally:
        if session is not None:
            session.close()
        shutil.rmtree(fixture_root, ignore_errors=True)

    return failures


def main():
    diredd = diredd_path()
    if not os.path.exists(diredd):
        print(
            "error: {} not found; build it (make -C test/integration test, "
            "or ./builder debug clean from the repo root)".format(diredd),
            file=sys.stderr,
        )
        return 1

    test_files = discover_tests()
    if not test_files:
        print("no *.json integration tests found")
        return 0

    total = 0
    failed = 0
    for path in test_files:
        total += 1
        name = os.path.basename(path)
        failures = run_test_file(path, diredd)
        if failures:
            failed += 1
            print("FAIL {}".format(name))
            for message in failures:
                print("  {}".format(message))
        else:
            print("PASS {}".format(name))

    print("{}/{} passed".format(total - failed, total))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

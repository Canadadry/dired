#!/usr/bin/env python3
"""Tests for strip-comments.py.

Run with: python3 scripts/git-hooks/test_strip_comments.py
"""
import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))

spec = importlib.util.spec_from_file_location(
    "strip_comments", os.path.join(HERE, "strip-comments.py")
)
strip_comments_mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(strip_comments_mod)

strip_comments = strip_comments_mod.strip_comments


def all_lines(text):
    """Target-set helper: treat every line of `text` as newly added."""
    return set(range(1, text.count("\n") + 2))


class FindAndStripComments(unittest.TestCase):
    def test_line_comment_on_added_line_is_removed(self):
        text = "int x = 1; // drop me\n"
        out, changed = strip_comments(text, all_lines(text))
        self.assertTrue(changed)
        self.assertEqual(out, "int x = 1;\n")

    def test_line_comment_todo_is_kept(self):
        text = "int x = 1; // TODO: drop me\n"
        out, changed = strip_comments(text, all_lines(text))
        self.assertFalse(changed)
        self.assertEqual(out, text)

    def test_line_comment_todo_case_insensitive(self):
        text = "int x = 1; // ToDo drop me\n"
        out, changed = strip_comments(text, all_lines(text))
        self.assertFalse(changed)
        self.assertEqual(out, text)

    def test_comment_only_line_is_deleted_entirely(self):
        text = "int x = 1;\n// disposable\nint y = 2;\n"
        out, changed = strip_comments(text, all_lines(text))
        self.assertTrue(changed)
        self.assertEqual(out, "int x = 1;\nint y = 2;\n")

    def test_unchanged_line_is_never_touched(self):
        text = "int x = 1; // drop me\n"
        out, changed = strip_comments(text, set())  # nothing is "added"
        self.assertFalse(changed)
        self.assertEqual(out, text)

    def test_single_line_block_comment_is_removed(self):
        text = "int x = 1; /* inline note */\n"
        out, changed = strip_comments(text, all_lines(text))
        self.assertTrue(changed)
        self.assertEqual(out, "int x = 1;\n")

    def test_block_comment_todo_is_kept(self):
        text = "/* TODO: revisit */\nint x = 1;\n"
        out, changed = strip_comments(text, all_lines(text))
        self.assertFalse(changed)
        self.assertEqual(out, text)

    def test_multiline_block_comment_fully_added_is_removed(self):
        text = (
            "int x = 1;\n"
            "/*\n"
            " * old notes\n"
            " * more notes\n"
            " */\n"
            "int y = 2;\n"
        )
        out, changed = strip_comments(text, all_lines(text))
        self.assertTrue(changed)
        self.assertEqual(out, "int x = 1;\nint y = 2;\n")

    def test_multiline_block_comment_partially_unchanged_is_left_alone(self):
        text = (
            "/*\n"
            " * pre-existing block, only line 3 below was added\n"
            " */\n"
            "int y = 2;\n"
        )
        # Only line 4 ("int y = 2;") is new; the comment block (lines 1-3)
        # predates this diff and must be left untouched.
        out, changed = strip_comments(text, {4})
        self.assertFalse(changed)
        self.assertEqual(out, text)

    def test_slash_slash_inside_block_comment_does_not_truncate_it(self):
        # Regression test: a `//` inside a /* */ block (e.g. a URL) must
        # not be mistaken for a line comment start, which used to eat the
        # rest of the block comment and leave a dangling `/*`.
        text = "int x = 1; /* see http://example.com for details */\n"
        out, changed = strip_comments(text, all_lines(text))
        self.assertTrue(changed)
        self.assertEqual(out, "int x = 1;\n")
        self.assertNotIn("/*", out)
        self.assertNotIn("http:", out)

    def test_code_after_multiline_block_comment_is_preserved(self):
        text = "foo(); /* start\nmiddle line\nend */ bar();\n"
        out, changed = strip_comments(text, all_lines(text))
        self.assertTrue(changed)
        self.assertEqual(out, "foo();\n bar();\n")

    def test_slash_slash_inside_string_literal_is_not_a_comment(self):
        text = 'char *url = "http://example.com"; // real comment\n'
        out, changed = strip_comments(text, all_lines(text))
        self.assertTrue(changed)
        self.assertEqual(out, 'char *url = "http://example.com";\n')

    def test_block_markers_inside_string_literal_are_not_a_comment(self):
        text = 'char *s = "/* not a comment */"; int x = 1;\n'
        out, changed = strip_comments(text, all_lines(text))
        self.assertFalse(changed)
        self.assertEqual(out, text)

    def test_no_eligible_comments_reports_unchanged(self):
        text = "int x = 1;\nint y = 2;\n"
        out, changed = strip_comments(text, all_lines(text))
        self.assertFalse(changed)
        self.assertEqual(out, text)


class PreCommitHookIntegration(unittest.TestCase):
    """End-to-end: run the hook as git would, against a real staged diff."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = self.tmp.name
        self._git("init", "-q")
        self._git("config", "user.email", "test@test.com")
        self._git("config", "user.name", "test")

    def tearDown(self):
        self.tmp.cleanup()

    def _git(self, *args):
        return subprocess.run(
            ["git", *args], cwd=self.repo, capture_output=True, text=True, check=True
        ).stdout

    def _write(self, name, content):
        with open(os.path.join(self.repo, name), "w") as f:
            f.write(content)

    def _read(self, name):
        with open(os.path.join(self.repo, name)) as f:
            return f.read()

    def _run_hook(self):
        subprocess.run(
            [sys.executable, os.path.join(HERE, "strip-comments.py")],
            cwd=self.repo,
            check=True,
        )

    def test_hook_strips_new_lines_and_restages(self):
        self._write("a.c", "int old(void) {\n    return 1; // pre-existing\n}\n")
        self._git("add", "a.c")
        self._git("commit", "-qm", "init")

        self._write(
            "a.c",
            "int old(void) {\n"
            "    return 1; // pre-existing\n"
            "}\n"
            "\n"
            "int fresh(void) {\n"
            "    return 2; /* see http://example.com */\n"
            "}\n",
        )
        self._git("add", "a.c")
        self._run_hook()

        self.assertEqual(
            self._read("a.c"),
            "int old(void) {\n"
            "    return 1; // pre-existing\n"
            "}\n"
            "\n"
            "int fresh(void) {\n"
            "    return 2;\n"
            "}\n",
        )
        # the stripped result must be what actually gets committed
        staged = self._git("diff", "--cached", "a.c")
        self.assertIn("+    return 2;\n", staged)
        self.assertNotIn("http://example.com", staged)

    def test_hook_ignores_non_c_files(self):
        self._write("notes.md", "line\n")
        self._git("add", "notes.md")
        self._git("commit", "-qm", "init")

        self._write("notes.md", "line\n// looks like a comment but is markdown\n")
        self._git("add", "notes.md")
        self._run_hook()

        self.assertEqual(
            self._read("notes.md"), "line\n// looks like a comment but is markdown\n"
        )

    def test_unstaged_file_is_completely_untouched(self):
        # a.c is staged and has a new disposable comment; b.c has a
        # disposable comment too but is never staged at all.
        self._write("a.c", "int a(void) { return 1; }\n")
        self._write("b.c", "int b(void) { return 2; } // pre-existing, disposable\n")
        self._git("add", "a.c", "b.c")
        self._git("commit", "-qm", "init")

        self._write(
            "a.c", "int a(void) { return 1; } // new disposable comment\n"
        )
        self._write(
            "b.c",
            "int b(void) { return 2; } // pre-existing, disposable\n"
            "int c(void) { return 3; } // also unstaged, disposable\n",
        )
        self._git("add", "a.c")  # only a.c staged, b.c left as unstaged edit
        before_b = self._read("b.c")
        self._run_hook()

        self.assertEqual(self._read("a.c"), "int a(void) { return 1; }\n")
        # b.c was never staged: the hook must not touch it at all, even
        # though it has plain disposable comments sitting right there.
        self.assertEqual(self._read("b.c"), before_b)
        status = self._git("status", "--porcelain", "b.c")
        self.assertTrue(status.startswith(" M"), status)  # unstaged mod only

    def test_preexisting_disposable_comments_survive_unrelated_edit(self):
        self._write(
            "a.c",
            "int one(void) { return 1; } // disposable, line 1\n"
            "int two(void) { return 2; } // disposable, line 2\n"
            "int three(void) { return 3; } // disposable, line 3\n",
        )
        self._git("add", "a.c")
        self._git("commit", "-qm", "init")

        # only touch line 2; lines 1 and 3 are untouched by this diff
        self._write(
            "a.c",
            "int one(void) { return 1; } // disposable, line 1\n"
            "int two(void) { return 22; } // disposable, line 2, edited\n"
            "int three(void) { return 3; } // disposable, line 3\n",
        )
        self._git("add", "a.c")
        self._run_hook()

        self.assertEqual(
            self._read("a.c"),
            "int one(void) { return 1; } // disposable, line 1\n"
            "int two(void) { return 22; }\n"
            "int three(void) { return 3; } // disposable, line 3\n",
        )


if __name__ == "__main__":
    unittest.main()

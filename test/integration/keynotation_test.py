import unittest

from keynotation import encode_keys


class EncodeKeysTest(unittest.TestCase):
    def test_literal_characters(self):
        cases = [
            ("single letter", ["a"], b"a"),
            ("multiple literal tokens", ["a", "b", "c"], b"abc"),
            ("multi-character token", ["abc"], b"abc"),
        ]
        for desc, keys, expected in cases:
            with self.subTest(desc):
                self.assertEqual(encode_keys(keys), expected)

    def test_enter_and_esc(self):
        cases = [
            ("enter", ["<Enter>"], b"\x0d"),
            ("esc", ["<Esc>"], b"\x1b"),
            ("literal then enter", ["a", "<Enter>"], b"a\x0d"),
        ]
        for desc, keys, expected in cases:
            with self.subTest(desc):
                self.assertEqual(encode_keys(keys), expected)

    def test_arrow_keys(self):
        cases = [
            ("up", ["<Up>"], b"\x1bOA"),
            ("down", ["<Down>"], b"\x1bOB"),
            ("right", ["<Right>"], b"\x1bOC"),
            ("left", ["<Left>"], b"\x1bOD"),
            ("navigate then activate", ["<Down>", "<Down>", "<Enter>"], b"\x1bOB\x1bOB\x0d"),
        ]
        for desc, keys, expected in cases:
            with self.subTest(desc):
                self.assertEqual(encode_keys(keys), expected)

    def test_backspace_and_delete(self):
        cases = [
            ("backspace", ["<BS>"], b"\x7f"),
            ("delete", ["<Del>"], b"\x1b[3~"),
            ("typo correction", ["a", "b", "<BS>", "c"], b"ab\x7fc"),
        ]
        for desc, keys, expected in cases:
            with self.subTest(desc):
                self.assertEqual(encode_keys(keys), expected)


if __name__ == "__main__":
    unittest.main()

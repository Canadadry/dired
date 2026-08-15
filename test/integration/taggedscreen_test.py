import unittest

from taggedscreen import Cell, format_row, format_screen, parse


class FormatRowTest(unittest.TestCase):
    def test_untagged_default(self):
        cases = [
            ("empty row", [], ""),
            ("single default char", [Cell("a", None, None, False)], "a"),
            (
                "multiple default chars",
                [Cell("a", None, None, False), Cell("b", None, None, False)],
                "ab",
            ),
        ]
        for desc, row, expected in cases:
            with self.subTest(desc):
                self.assertEqual(format_row(row), expected)

    def test_fg_only(self):
        cases = [
            ("red", [Cell("a", "red", None, False)], "<red>a</red>"),
            ("green", [Cell("a", "green", None, False)], "<green>a</green>"),
            ("yellow", [Cell("a", "yellow", None, False)], "<yellow>a</yellow>"),
            ("blue", [Cell("a", "blue", None, False)], "<blue>a</blue>"),
            ("magenta", [Cell("a", "magenta", None, False)], "<magenta>a</magenta>"),
            ("white", [Cell("a", "white", None, False)], "<white>a</white>"),
            ("black", [Cell("a", "black", None, False)], "<black>a</black>"),
            (
                "run of same color merges into one tag",
                [Cell("f", "yellow", None, False), Cell("o", "yellow", None, False)],
                "<yellow>fo</yellow>",
            ),
        ]
        for desc, row, expected in cases:
            with self.subTest(desc):
                self.assertEqual(format_row(row), expected)

    def test_bright_wraps_color(self):
        cases = [
            ("bright red", [Cell("a", "red", None, True)], "<bright><red>a</red></bright>"),
            (
                "bright run merges",
                [Cell("x", "black", None, True), Cell("y", "black", None, True)],
                "<bright><black>xy</black></bright>",
            ),
        ]
        for desc, row, expected in cases:
            with self.subTest(desc):
                self.assertEqual(format_row(row), expected)

    def test_bg_wraps_fg(self):
        cases = [
            ("bg only, default fg", [Cell("a", None, "blue", False)], "<bg-blue>a</bg-blue>"),
            (
                "bg wraps fg",
                [Cell("a", "black", "yellow", False)],
                "<bg-yellow><black>a</black></bg-yellow>",
            ),
        ]
        for desc, row, expected in cases:
            with self.subTest(desc):
                self.assertEqual(format_row(row), expected)

    def test_mixed_row(self):
        row = [
            Cell("a", None, None, False),
            Cell("f", "yellow", None, False),
            Cell("i", "yellow", None, False),
            Cell("l", "yellow", None, False),
            Cell("e", "yellow", None, False),
            Cell(" ", None, None, False),
            Cell("x", "black", "yellow", False),
        ]
        expected = "a<yellow>file</yellow> <bg-yellow><black>x</black></bg-yellow>"
        self.assertEqual(format_row(row), expected)


class FormatScreenTest(unittest.TestCase):
    def test_multiple_rows(self):
        screen = [
            [Cell("a", None, None, False)],
            [Cell("b", "red", None, False)],
        ]
        self.assertEqual(format_screen(screen), ["a", "<red>b</red>"])


class ParseTest(unittest.TestCase):
    def test_untagged_default(self):
        self.assertEqual(
            parse("ab"),
            [Cell("a", None, None, False), Cell("b", None, None, False)],
        )

    def test_fg_only(self):
        cases = [
            ("red", "<red>a</red>", [Cell("a", "red", None, False)]),
            ("green", "<green>a</green>", [Cell("a", "green", None, False)]),
            ("yellow", "<yellow>a</yellow>", [Cell("a", "yellow", None, False)]),
            ("blue", "<blue>a</blue>", [Cell("a", "blue", None, False)]),
            ("magenta", "<magenta>a</magenta>", [Cell("a", "magenta", None, False)]),
            ("white", "<white>a</white>", [Cell("a", "white", None, False)]),
            ("black", "<black>a</black>", [Cell("a", "black", None, False)]),
        ]
        for desc, s, expected in cases:
            with self.subTest(desc):
                self.assertEqual(parse(s), expected)

    def test_bright_wraps_color(self):
        self.assertEqual(
            parse("<bright><red>a</red></bright>"),
            [Cell("a", "red", None, True)],
        )

    def test_bg_wraps_fg(self):
        cases = [
            ("bg only", "<bg-blue>a</bg-blue>", [Cell("a", None, "blue", False)]),
            (
                "bg wraps fg",
                "<bg-yellow><black>a</black></bg-yellow>",
                [Cell("a", "black", "yellow", False)],
            ),
        ]
        for desc, s, expected in cases:
            with self.subTest(desc):
                self.assertEqual(parse(s), expected)

    def test_list_of_rows(self):
        self.assertEqual(
            parse(["a", "<red>b</red>"]),
            [[Cell("a", None, None, False)], [Cell("b", "red", None, False)]],
        )


class RoundTripTest(unittest.TestCase):
    def test_row_round_trip(self):
        rows = [
            ("all default", [Cell("a", None, None, False)]),
            (
                "color run",
                [Cell("f", "yellow", None, False), Cell("o", "yellow", None, False)],
            ),
            ("bright color", [Cell("x", "black", None, True)]),
            ("bg only", [Cell("a", None, "blue", False)]),
            ("bg wraps fg", [Cell("a", "black", "yellow", False)]),
            (
                "mixed row",
                [
                    Cell("a", None, None, False),
                    Cell("f", "yellow", None, False),
                    Cell("i", "yellow", None, False),
                    Cell(" ", None, None, False),
                    Cell("x", "black", "yellow", False),
                ],
            ),
        ]
        for desc, row in rows:
            with self.subTest(desc):
                self.assertEqual(parse(format_row(row)), row)

    def test_screen_round_trip(self):
        screen = [
            [Cell("a", None, None, False), Cell("b", "green", None, False)],
            [Cell("c", "black", "yellow", False)],
        ]
        self.assertEqual(parse(format_screen(screen)), screen)


if __name__ == "__main__":
    unittest.main()

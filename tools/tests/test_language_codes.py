import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class LanguageCodesTest(unittest.TestCase):
    def test_the_player_looks_for_the_codes_in_languages_json_order(self):
        languages = json.loads((ROOT / "assets/lang/languages.json").read_text())["languages"]
        source = (ROOT / "src/app/language_files.c").read_text()
        table = re.search(r"LANGUAGE_CODES\[LANGUAGE_COUNT\]\[3\] = \{(.*?)\};", source, re.S)
        self.assertIsNotNone(table)
        self.assertEqual([language["code"] for language in languages],
                         re.findall(r'"(\w\w)"', table.group(1)))

    def test_built_in_languages_come_first(self):
        languages = json.loads((ROOT / "assets/lang/languages.json").read_text())["languages"]
        flags = [bool(language.get("builtin")) for language in languages]
        self.assertEqual(sorted(flags, reverse=True), flags)
        header = (ROOT / "src/app/language_files.h").read_text()
        count = int(re.search(r"LANGUAGE_BUILTIN_COUNT = (\d+)", header).group(1))
        self.assertEqual(count, sum(flags))


if __name__ == "__main__":
    unittest.main()

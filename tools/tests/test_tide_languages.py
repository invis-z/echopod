import io
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tide import languages


class TideLanguagesTest(unittest.TestCase):
    def test_every_optional_language_has_a_pack_the_player_accepts(self):
        optional = languages.optional_languages()
        self.assertEqual(16, len(optional))
        for language in optional:
            data = (languages.pack_folder() / f"{language['code']}.bin").read_bytes()
            self.assertIsNone(languages.pack_problem(data), language["code"])

    def test_built_in_languages_are_not_offered(self):
        codes = [language["code"] for language in languages.optional_languages()]
        self.assertNotIn("en", codes)
        self.assertNotIn("zh", codes)

    def test_broken_packs_are_named(self):
        self.assertIsNotNone(languages.pack_problem(b"ELNG"))
        self.assertIsNotNone(languages.pack_problem(b"XXXX" + bytes(20)))
        self.assertIsNotNone(languages.pack_problem(b"ELNG" + bytes(languages.PACK_LIMIT)))

    def test_install_copies_the_packs_into_the_languages_folder_and_remove_deletes_them(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            drive = Path(folder)
            self.assertEqual(0, languages.main(["install", "de", "ru", "--drive", folder]))
            self.assertTrue((drive / "Languages" / "de.bin").is_file())
            self.assertEqual((languages.pack_folder() / "ru.bin").read_bytes(),
                             (drive / "Languages" / "ru.bin").read_bytes())
            self.assertEqual(0, languages.main(["remove", "de", "--drive", folder]))
            self.assertFalse((drive / "Languages" / "de.bin").exists())

    def test_unknown_and_built_in_codes_are_refused_and_nothing_is_written(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            self.assertEqual(2, languages.main(["install", "zh", "--drive", folder]))
            self.assertEqual(2, languages.main(["install", "xx", "--drive", folder]))
            self.assertFalse((Path(folder) / "Languages").exists())

    def test_a_dry_run_writes_nothing(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            self.assertEqual(0, languages.main(["install", "de", "--drive", folder, "--dry-run"]))
            self.assertFalse((Path(folder) / "Languages").exists())


if __name__ == "__main__":
    unittest.main()

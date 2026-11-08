import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import save_files


class SaveFilesTest(unittest.TestCase):
    def test_missing_files_are_created_zero_filled(self):
        with tempfile.TemporaryDirectory() as directory:
            rippl = Path(directory)
            save_files.prepare(rippl)
            for name in save_files.SAVE_FILES:
                data = (rippl / name).read_bytes()
                self.assertEqual(save_files.SAVE_FILE_SIZE, len(data))
                self.assertEqual(bytes(save_files.SAVE_FILE_SIZE), data)

    def test_short_files_keep_their_bytes_and_are_padded(self):
        with tempfile.TemporaryDirectory() as directory:
            rippl = Path(directory)
            (rippl / "settings.bin").write_bytes(b"EMST" + bytes(range(20)))
            save_files.prepare(rippl)
            data = (rippl / "settings.bin").read_bytes()
            self.assertEqual(save_files.SAVE_FILE_SIZE, len(data))
            self.assertEqual(b"EMST" + bytes(range(20)), data[:24])
            self.assertEqual(bytes(save_files.SAVE_FILE_SIZE - 24), data[24:])

    def test_files_already_big_enough_are_left_alone(self):
        with tempfile.TemporaryDirectory() as directory:
            rippl = Path(directory)
            original = bytes([7]) * (save_files.SAVE_FILE_SIZE + 10)
            (rippl / "favorites.bin").write_bytes(original)
            save_files.prepare(rippl)
            self.assertEqual(original, (rippl / "favorites.bin").read_bytes())

    def test_the_folder_is_created_when_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            rippl = Path(directory) / ".rippl"
            save_files.prepare(rippl)
            self.assertTrue((rippl / "resume.bin").is_file())


class LibraryRoomTest(unittest.TestCase):
    def test_the_library_gets_its_reserved_room_and_keeps_its_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            rippl = Path(directory)
            (rippl / "library.db").write_bytes(b"EMDB" + bytes(100))
            save_files.prepare(rippl)
            data = (rippl / "library.db").read_bytes()
            self.assertEqual(save_files.LIBRARY_FILE_SIZE, len(data))
            self.assertEqual(b"EMDB", data[:4])

    def test_a_missing_library_is_reserved_as_zeros(self):
        with tempfile.TemporaryDirectory() as directory:
            rippl = Path(directory)
            save_files.prepare(rippl)
            data = (rippl / "library.db").read_bytes()
            self.assertEqual(save_files.LIBRARY_FILE_SIZE, len(data))
            self.assertEqual(0, max(data))


if __name__ == "__main__":
    unittest.main()

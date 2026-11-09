import contextlib
import io
import os
import struct
import sys
import tempfile
import unicodedata
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest import mock
import zoneinfo
from zoneinfo import ZoneInfo

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import cover_export
from tide import rippl_format, store as tide_store
from tide import daylight
from tide import sync as tide_sync
import library_index
from test_cover_export import image_bytes
from test_library_index import write_silent_flac

SCAN_RECORD = struct.Struct("<4sIII16s")
MEBIBYTE = 1024 * 1024
FIXED_UTC = tide_sync.CardVolume("FAT32", True, False, 0, "")
EXFAT = tide_sync.CardVolume("exFAT", True, True, 0, "")
VOLUME_LABEL = tide_sync.volume_label


class MemoryCard:
    def __init__(self):
        self.nodes = []

    def add(self, parent, name, directory, size, modified):
        self.nodes.append([parent, name, directory, size, modified])

    def node(self, name):
        return next(node for node in self.nodes if node[1] == name)

    def list_folder(self, path):
        if path != "/" and not any(node[0] == path for node in self.nodes):
            return None
        return [tide_sync.CardEntry(name.encode("utf-8"), directory, size, modified,
                                    tide_sync.child_path(parent.encode(), name.encode()).decode())
                for parent, name, directory, size, modified in self.nodes if parent == path]

    def fingerprint(self, excluded=frozenset()):
        return tide_sync.music_fingerprint(self.list_folder, "/", excluded)


def players_test_card():
    card = MemoryCard()
    card.add("/", "AC DC", True, 0, 1)
    card.add("/AC DC", "Back In Black", True, 0, 2)
    card.add("/AC DC/Back In Black", "01 - Hells Bells.flac", False, 31000000, 100)
    card.add("/AC DC/Back In Black", "02 - Shoot to Thrill.flac", False, 29000000, 101)
    card.add("/AC DC/Back In Black", "cover.jpg", False, 80000, 102)
    card.add("/", "Mix.m3u8", False, 400, 3)
    card.add("/", ".rippl", True, 0, 4)
    card.add("/.rippl", "library.db", False, 4194304, 5)
    return card


class PlayerFingerprintTest(unittest.TestCase):
    def test_the_players_test_card_gives_the_fingerprint_the_player_stores(self):
        self.assertEqual(tide_sync.Fingerprint(True, 3518657026, 2077149385, 6),
                         players_test_card().fingerprint())

    def test_internal_storage_folds_in_as_the_last_root_folder_as_on_the_player(self):
        card = players_test_card()
        card.add("/", ":internal", True, 0, 0)
        card.add("/:internal", "Artist", True, 0, 11)
        card.add("/:internal/Artist", "01 - Song.flac", False, 5000000, 12)
        card.add("/:internal", "System Volume Information", True, 0, 13)
        card.add("/:internal", "HIFIEC90.IMG", False, 9000000, 14)
        self.assertEqual(tide_sync.Fingerprint(True, 1993034583, 148508692, 9),
                         card.fingerprint())

    def test_folders_that_cannot_be_opened_count_as_empty_as_on_the_player(self):
        card = players_test_card()
        card.add("/", "Empty", True, 0, 6)
        card.add("/AC DC", "Live", True, 0, 7)
        self.assertEqual(tide_sync.Fingerprint(True, 732559337, 1706284886, 8),
                         card.fingerprint())

    def test_a_left_out_internal_folder_gives_the_fingerprint_the_player_stores(self):
        card = players_test_card()
        card.add("/", ":internal", True, 0, 0)
        card.add("/:internal", "Artist", True, 0, 11)
        card.add("/:internal/Artist", "01 - Song.flac", False, 5000000, 12)
        card.add("/:internal", "Album", True, 0, 15)
        card.add("/:internal/Album", "02 - Other.flac", False, 6000000, 16)
        card_artist = frozenset({library_index.folder_key(b"Artist")})
        self.assertEqual(card.fingerprint(), card.fingerprint(card_artist))
        internal_artist = frozenset({library_index.folder_key(b"Artist", internal=True)})
        self.assertEqual(tide_sync.Fingerprint(True, 4104151954, 1967075651, 9),
                         card.fingerprint(internal_artist))

    def test_a_left_out_folder_counts_as_if_it_were_not_there(self):
        card = players_test_card()
        before = card.fingerprint()
        card.add("/", "Spoken", True, 0, 20)
        card.add("/Spoken", "Talk.mp3", False, 3000, 21)
        left_out = frozenset({library_index.folder_key(b"Spoken")})
        self.assertNotEqual(before, card.fingerprint())
        self.assertEqual(before, card.fingerprint(left_out))
        card.add("/AC DC", "Spoken", True, 0, 22)
        card.add("/AC DC/Spoken", "Inner.mp3", False, 3000, 23)
        self.assertNotEqual(before, card.fingerprint(left_out))

    def test_hidden_entries_do_not_count(self):
        card = players_test_card()
        before = card.fingerprint()
        card.add("/", ".fseventsd", True, 0, 9)
        card.add("/.fseventsd", "0000000001", False, 77, 9)
        card.add("/AC DC/Back In Black", "._01 - Hells Bells.flac", False, 4096, 9)
        card.node("library.db")[4] = 999
        self.assertEqual(before, card.fingerprint())

    def test_files_the_library_ignores_do_not_count(self):
        card = players_test_card()
        before = card.fingerprint()
        card.add("/", "probe1.bin", False, 65536, 9)
        card.add("/AC DC/Back In Black", "notes.txt", False, 120, 9)
        card.add("/AC DC/Back In Black", "scan 01.jpg", False, 900000, 9)
        card.add("/", "System Volume Information", True, 0, 9)
        card.add("/System Volume Information", "WPSettings.dat", False, 12, 9)
        self.assertEqual(before, card.fingerprint())

    def test_the_counted_names_are_the_players(self):
        counted = ["a.flac", "a.ogg", "a.opus", "a.mp3", "a.m4a", "a.wav", "a.wma", "a.ape", "a.dsf",
                   "a.dff", "A.FLAC", "b.m3u", "b.M3U8", "Folder.JPG", "front.png",
                   "albumart.jpeg", "cover.png"]
        ignored = ["x.aac", "x.txt", "x.aiff", "x.mp4", "x.flac.txt", "scan 01.jpg",
                   "covers.jpg", "folder.gif"]
        for name in counted:
            self.assertTrue(tide_sync.fingerprint_counts(name.encode(), False), name)
        for name in ignored:
            self.assertFalse(tide_sync.fingerprint_counts(name.encode(), False), name)
        self.assertTrue(tide_sync.fingerprint_counts(b"notes.txt", True))
        self.assertFalse(tide_sync.fingerprint_counts(b"SYSTEM VOLUME INFORMATION", True))

    def test_a_changed_stamp_size_or_name_changes_it(self):
        before = players_test_card().fingerprint()
        for index, field in ((2, 4), (4, 3)):
            card = players_test_card()
            card.nodes[index][field] += 1
            self.assertNotEqual(before, card.fingerprint())
        card = players_test_card()
        card.node("Mix.m3u8")[1] = "Mix 2.m3u8"
        self.assertNotEqual(before, card.fingerprint())

    def test_a_card_deeper_than_the_player_walks_is_incomplete(self):
        card = MemoryCard()
        parent = "/"
        for _ in range(tide_sync.MAX_DEPTH + 1):
            card.add(parent, "d", True, 0, 1)
            parent = tide_sync.child_path(parent.encode(), b"d").decode()
        self.assertFalse(card.fingerprint().complete)

    def test_the_deepest_folder_the_player_walks_is_complete(self):
        card = MemoryCard()
        parent = "/"
        for _ in range(tide_sync.MAX_DEPTH - 1):
            card.add(parent, "d", True, 0, 1)
            parent = tide_sync.child_path(parent.encode(), b"d").decode()
        self.assertTrue(card.fingerprint().complete)

    def test_a_path_longer_than_the_player_holds_is_incomplete(self):
        card = MemoryCard()
        card.add("/", "a" * 1022, True, 0, 1)
        self.assertTrue(card.fingerprint().complete)
        card = MemoryCard()
        card.add("/", "a" * 1023, True, 0, 1)
        self.assertFalse(card.fingerprint().complete)

    def test_a_card_without_a_root_is_incomplete(self):
        self.assertFalse(tide_sync.music_fingerprint(lambda location: None, "/").complete)


class StampTest(unittest.TestCase):
    def test_dates_and_times_pack_like_a_fat_directory_entry(self):
        seconds = int(datetime(2026, 1, 1, tzinfo=timezone.utc).timestamp())
        self.assertEqual(0x5C210000, tide_sync.dos_stamp(seconds))
        seconds = int(datetime(2024, 7, 9, 13, 45, 59, tzinfo=timezone.utc).timestamp())
        expected = ((2024 - 1980) << 9 | 7 << 5 | 9) << 16 | 13 << 11 | 45 << 5 | 29
        self.assertEqual(expected, tide_sync.dos_stamp(seconds))

    def test_times_fat_cannot_hold_are_zero(self):
        self.assertEqual(0, tide_sync.dos_stamp(0))
        self.assertEqual(0, tide_sync.dos_stamp(-11644473600))

    def test_a_fixed_offset_shifts_utc_to_the_cards_clock(self):
        volume = tide_sync.CardVolume("FAT32", True, False, 3600, "")
        stat = FakeStat(datetime(2025, 3, 1, 10, 0, 0, tzinfo=timezone.utc))
        entry = tide_sync.host_entry(FakeEntry("a.flac", stat), volume)
        self.assertEqual(tide_sync.dos_stamp(stat.st_mtime_ns // 10 ** 9 + 3600), entry.modified)

    def test_hundredths_of_a_second_are_dropped_as_the_player_does(self):
        written = datetime(2025, 3, 1, 10, 0, 0, tzinfo=timezone.utc)
        exact = tide_sync.host_entry(FakeEntry("a.flac", FakeStat(written)), FIXED_UTC)
        later = FakeStat(written, extra_ns=1_990_000_000)
        self.assertEqual(exact.modified,
                         tide_sync.host_entry(FakeEntry("a.flac", later), FIXED_UTC).modified)

    def test_exfat_times_are_utc_half_seconds_as_the_player_computes(self):
        written = datetime(2026, 7, 11, 13, 32, 22, tzinfo=timezone.utc)
        entry = tide_sync.host_entry(FakeEntry("a.flac", FakeStat(written)), EXFAT)
        self.assertEqual(891888371, entry.modified)
        later = FakeStat(written, extra_ns=1_990_000_000)
        self.assertEqual(891888371, tide_sync.host_entry(FakeEntry("a.flac", later), EXFAT).modified)

    def test_exfat_needs_no_time_zone_guess_on_any_computer(self):
        for platform in ("win32", "darwin", "linux"):
            volume = tide_sync.card_volume("exFAT", "", platform)
            self.assertTrue(volume.exfat)
            self.assertEqual("", volume.caveat)

    def test_exfat_files_of_4_gib_or_more_are_left_out_as_the_player_does(self):
        volume = EXFAT
        stat = FakeStat(datetime(2025, 1, 1, tzinfo=timezone.utc), size=1 << 32)
        self.assertIsNone(tide_sync.host_entry(FakeEntry("big.wav", stat), volume))
        self.assertIsNotNone(tide_sync.host_entry(FakeEntry("big.wav", stat), FIXED_UTC))

    def test_folders_count_with_size_zero(self):
        stat = FakeStat(datetime(2025, 1, 1, tzinfo=timezone.utc), size=4096)
        entry = tide_sync.host_entry(FakeEntry("Album", stat, directory=True), FIXED_UTC)
        self.assertEqual((True, 0), (entry.directory, entry.size))


class FakeStat:
    def __init__(self, moment, size=1000, birth=None, extra_ns=0):
        self.st_mtime_ns = int(moment.timestamp()) * 10 ** 9 + extra_ns
        self.st_mtime = self.st_mtime_ns / 10 ** 9
        self.st_birthtime = self.st_mtime if birth is None else birth
        self.st_size = size


class FakeEntry:
    def __init__(self, name, stat, directory=False):
        self.name, self.path, self.stat_result, self.directory = name, name, stat, directory

    def stat(self, follow_symlinks=True):
        return self.stat_result

    def is_dir(self, follow_symlinks=True):
        return self.directory


class NameTest(unittest.TestCase):
    def test_names_are_utf8(self):
        self.assertEqual("Ältere.m3u".encode("utf-8"), tide_sync.card_name("Ältere.m3u", "win32"))

    def test_a_mac_lists_names_decomposed_and_they_are_recomposed(self):
        decomposed = unicodedata.normalize("NFD", "Ältere.m3u")
        self.assertEqual("Ältere.m3u".encode("utf-8"), tide_sync.card_name(decomposed, "darwin"))

    def test_an_unpaired_surrogate_is_encoded_as_the_player_does(self):
        self.assertEqual(b"a\xed\xa0\x80", tide_sync.card_name("a\ud800", "win32"))


class HostCardTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def make(self, relative, data=b"x", moment=datetime(2024, 5, 6, 7, 8, 10, tzinfo=timezone.utc)):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        seconds = moment.timestamp()
        os.utime(path, (seconds, seconds))
        return path

    def listed_card(self):
        card = MemoryCard()
        folders = [(str(self.root), "/")]
        while folders:
            folder, card_folder = folders.pop(0)
            for entry in os.scandir(folder):
                stat = entry.stat()
                modified = tide_sync.dos_stamp(stat.st_mtime_ns // 10 ** 9)
                card.add(card_folder, entry.name, entry.is_dir(),
                         0 if entry.is_dir() else stat.st_size, modified)
                if entry.is_dir():
                    path = tide_sync.child_path(card_folder.encode(), entry.name.encode())
                    folders.append((entry.path, path.decode()))
        return card

    def test_a_folder_on_disk_gives_the_fingerprint_of_its_listing(self):
        self.make("Artist/Album/01 - Song.flac", b"12345")
        self.make("Artist/Album/cover.jpg", b"123")
        self.make("Artist/Album/notes.txt")
        self.make(".rippl/library.db")
        self.make("Mix.m3u8", b"#EXTM3U")
        computed = tide_sync.card_fingerprint(self.root, FIXED_UTC)
        self.assertEqual(self.listed_card().fingerprint(), computed)
        self.assertEqual(5, computed.entries)


    def test_two_drives_list_the_internal_drive_as_the_last_root_folder(self):
        self.make("Artist/Album/01 - Song.flac", b"12345")
        with tempfile.TemporaryDirectory() as other:
            internal = Path(other)
            song = internal / "Band" / "02 - Tune.mp3"
            song.parent.mkdir()
            song.write_bytes(b"abc")
            seconds = datetime(2024, 5, 6, 7, 8, 10, tzinfo=timezone.utc).timestamp()
            os.utime(song, (seconds, seconds))
            os.utime(song.parent, (seconds, seconds))
            drive = tide_sync.InternalDrive(internal, FIXED_UTC)
            computed = tide_sync.card_fingerprint(self.root, FIXED_UTC, drive)
        stamp = tide_sync.dos_stamp(int(seconds))
        expected = self.listed_card()
        expected.add("/", ":internal", True, 0, 0)
        expected.add("/:internal", "Band", True, 0, stamp)
        expected.add("/:internal/Band", "02 - Tune.mp3", False, 3, stamp)
        self.assertEqual(expected.fingerprint(), computed)
        self.assertEqual(6, computed.entries)
        self.assertNotEqual(tide_sync.card_fingerprint(self.root, FIXED_UTC), computed)


class InternalDriveTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.top = Path(directory.name).resolve()
        self.labels = {}
        patcher = mock.patch.object(tide_sync, "volume_label",
                                    lambda root: self.labels.get(root.name))
        patcher.start()
        self.addCleanup(patcher.stop)

    def drive(self, name, label, rippl=False):
        root = self.top / name
        root.mkdir()
        if rippl:
            (root / ".rippl").mkdir()
        self.labels[name] = label
        return root

    def test_the_internal_drive_is_the_one_called_rippl(self):
        card = self.drive("E", "MUSIC", rippl=True)
        internal = self.drive("F", "RIPPL")
        stick = self.drive("G", "BACKUP")
        unlabelled = self.drive("H", None)
        self.assertEqual([internal], tide_sync.player_internal_drives(
            card, [card, stick, unlabelled, internal, self.top / "missing"]))
        self.labels["F"] = "rippl "
        self.assertEqual([internal], tide_sync.player_internal_drives(card, [card, internal]))
        self.assertEqual([], tide_sync.player_internal_drives(card, [card, stick]))

    def test_a_card_called_rippl_is_not_its_own_internal_storage(self):
        card = self.drive("E", "RIPPL", rippl=True)
        internal = self.drive("F", "RIPPL")
        self.assertEqual([internal], tide_sync.player_internal_drives(card, [card, internal]))
        self.assertEqual([], tide_sync.player_internal_drives(card, [card]))

    def test_a_drive_without_a_rippl_folder_wins_over_another_players_card(self):
        card = self.drive("E", "MUSIC", rippl=True)
        other_card = self.drive("F", "RIPPL", rippl=True)
        internal = self.drive("G", "RIPPL")
        self.assertEqual([internal],
                         tide_sync.player_internal_drives(card, [card, other_card, internal]))
        self.assertEqual([other_card], tide_sync.player_internal_drives(card, [card, other_card]))

    def test_outside_windows_the_label_is_the_mount_point_name(self):
        self.assertEqual("ECHO MINI", VOLUME_LABEL(Path("/Volumes/ECHO MINI"), "darwin"))
        self.assertEqual("ECHO MINI", VOLUME_LABEL(Path("/media/me/ECHO MINI"), "linux"))


class CardFileTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "file.bin"
        self.card_file = tide_sync.CardFile("file.bin", 64 * 1024, 16)

    def tearDown(self):
        self.directory.cleanup()

    def test_the_standard_sizes_are_the_ones_the_player_reserves(self):
        self.assertEqual(4096, tide_sync.SCAN_FILE.standard_size)

    def test_a_missing_file_is_created_at_the_standard_size(self):
        data = bytes(range(40))
        self.assertEqual(64 * 1024, tide_sync.write_card_file(self.path, data, self.card_file))
        written = self.path.read_bytes()
        self.assertEqual(64 * 1024, len(written))
        self.assertEqual(data, written[:40])
        self.assertEqual(bytes(64 * 1024 - 40), written[40:])

    def test_an_existing_file_is_written_in_place_and_keeps_its_size(self):
        self.path.write_bytes(b"\xAA" * 100_000)
        data = bytes(range(1, 41))
        self.assertEqual(100_000, tide_sync.write_card_file(self.path, data, self.card_file))
        written = self.path.read_bytes()
        self.assertEqual(100_000, len(written))
        self.assertEqual(data, written[:40])
        self.assertEqual(b"\xAA" * (100_000 - 40), written[40:])

    def test_a_file_too_small_for_the_data_grows_to_the_standard_size(self):
        self.path.write_bytes(b"\xAA" * 10)
        tide_sync.write_card_file(self.path, bytes(range(1, 41)), self.card_file)
        self.assertEqual(64 * 1024, self.path.stat().st_size)

    def test_data_bigger_than_the_standard_size_gets_a_file_of_its_size(self):
        data = b"\x01" * (100 * 1024)
        tide_sync.write_card_file(self.path, data, self.card_file)
        self.assertEqual(data, self.path.read_bytes())

    def test_forgetting_the_fingerprint_clears_only_its_magic(self):
        record = SCAN_RECORD.pack(b"MESC", 1, 2, 3, b"v1.2.3".ljust(16, b"\0"))
        self.path.write_bytes(record + bytes(4096 - len(record)))
        tide_sync.forget_fingerprint(self.path)
        written = self.path.read_bytes()
        self.assertEqual(bytes(4) + record[4:], written[:len(record)])
        self.assertEqual(4096, len(written))


class ScanRecordTest(unittest.TestCase):
    def test_the_record_holds_the_magic_both_hashes_the_count_and_the_firmware_tag(self):
        tag = b"0.1.0".ljust(16, b"\0")
        record = tide_sync.scan_record(tide_sync.Fingerprint(True, 0x11223344, 5, 6), tag)
        self.assertEqual(b"MESC" + bytes([0x44, 0x33, 0x22, 0x11]) + struct.pack("<II", 5, 6) + tag,
                         record)

    def test_a_stored_record_reads_back(self):
        fingerprint = tide_sync.Fingerprint(True, 1, 2, 3)
        record = tide_sync.scan_record(fingerprint, bytes(16))
        self.assertEqual(fingerprint, tide_sync.stored_fingerprint(record))
        self.assertIsNone(tide_sync.stored_fingerprint(bytes(32)))

    def test_the_firmware_tag_is_kept_or_empty(self):
        record = SCAN_RECORD.pack(b"MESC", 1, 2, 3, b"tag".ljust(16, b"\0"))
        self.assertEqual(b"tag".ljust(16, b"\0"), tide_sync.stored_version(record))
        self.assertEqual(bytes(16), tide_sync.stored_version(b""))


zoneinfo.reset_tzpath(to=[])


def zone_offsets(name):
    zone = ZoneInfo(name)

    def offset_at(utc_seconds):
        moment = datetime.fromtimestamp(utc_seconds, timezone.utc).astimezone(zone)
        return int(moment.utcoffset().total_seconds()) // 60
    return offset_at


class ClockRecordTest(unittest.TestCase):
    BERLIN = daylight.Rule(60, daylight.Edge(3, 5, 0, 120), daylight.Edge(10, 5, 0, 120))

    def test_the_record_holds_the_time_the_standard_offset_the_rule_and_a_checksum(self):
        record = tide_sync.clock_record(1_782_907_200, 60, self.BERLIN)
        self.assertEqual(32, len(record))
        self.assertEqual(b"ECLK\x01\x01\x00\x00", record[:8])
        self.assertEqual(struct.pack("<Ihh", 1_782_907_200, 60, 60), record[8:16])
        self.assertEqual(bytes([3, 5, 0, 0]) + struct.pack("<h", 120), record[16:22])
        self.assertEqual(bytes([10, 5, 0, 0]) + struct.pack("<h", 120), record[22:28])
        self.assertEqual(cover_export.fnv1a(record[:28]), struct.unpack("<I", record[28:])[0])
        self.assertEqual((1_782_907_200, 60, self.BERLIN), tide_sync.stored_clock(record))

    def test_a_zone_without_daylight_saving_has_no_rule(self):
        record = tide_sync.clock_record(1_793_880_000, -300)
        self.assertEqual(b"ECLK\x01\x00", record[:6])
        self.assertEqual(bytes(14), record[14:28])
        self.assertEqual((1_793_880_000, -300, None), tide_sync.stored_clock(record))

    def test_a_damaged_or_foreign_record_reads_as_none(self):
        record = bytearray(tide_sync.clock_record(1_793_880_000, 60))
        record[9] ^= 1
        self.assertIsNone(tide_sync.stored_clock(bytes(record)))
        self.assertIsNone(tide_sync.stored_clock(b"MESC" + bytes(28)))
        self.assertIsNone(tide_sync.stored_clock(b""))

    def test_the_record_is_described_in_local_time(self):
        self.assertEqual("2026-11-05 07:00 UTC-05:00", tide_sync.clock_text(1_793_880_000, -300))
        self.assertEqual("2026-11-05 17:30 UTC+05:30", tide_sync.clock_text(1_793_880_000, 330))
        self.assertEqual("2026-07-01 14:00 UTC+02:00, with daylight saving",
                         tide_sync.clock_text(1_782_907_200, 120, self.BERLIN))

    def test_write_clock_writes_the_record_unless_dry_or_out_of_range(self):
        def plus_one(_):
            return 60
        with tempfile.TemporaryDirectory() as directory:
            rippl = Path(directory)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                dry = tide_sync.write_clock(rippl, tide_sync.SyncOptions(dry_run=True),
                                            now=1_793_880_000, offset_at=plus_one)
                self.assertFalse(dry)
                self.assertFalse((rippl / "clock.bin").exists())
                self.assertFalse(tide_sync.write_clock(rippl, tide_sync.SyncOptions(),
                                                       now=1_000_000_000, offset_at=plus_one))
                self.assertFalse((rippl / "clock.bin").exists())
                self.assertTrue(tide_sync.write_clock(rippl, tide_sync.SyncOptions(),
                                                      now=1_793_880_000, offset_at=plus_one))
            self.assertEqual((1_793_880_000, 60, None),
                             tide_sync.stored_clock((rippl / "clock.bin").read_bytes()))
        self.assertIn("would write clock.bin: 2026-11-05 13:00 UTC+01:00", output.getvalue())
        self.assertIn("clock.bin   2026-11-05 13:00 UTC+01:00", output.getvalue())

    def test_a_computer_that_cant_look_ahead_still_writes_its_offset(self):
        def offset_at(utc_seconds):
            if utc_seconds > 1_793_880_000:
                raise OverflowError("timestamp out of range")
            return 120
        with tempfile.TemporaryDirectory() as directory:
            rippl = Path(directory)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertTrue(tide_sync.write_clock(rippl, tide_sync.SyncOptions(),
                                                      now=1_793_880_000, offset_at=offset_at))
            self.assertEqual((1_793_880_000, 120, None),
                             tide_sync.stored_clock((rippl / "clock.bin").read_bytes()))

    def test_a_summer_sync_writes_standard_time_and_the_rule(self):
        berlin = zone_offsets("Europe/Berlin")
        with tempfile.TemporaryDirectory() as directory:
            rippl = Path(directory)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertTrue(tide_sync.write_clock(rippl, tide_sync.SyncOptions(),
                                                      now=1_782_907_200, offset_at=berlin))
            self.assertEqual((1_782_907_200, 60, self.BERLIN),
                             tide_sync.stored_clock((rippl / "clock.bin").read_bytes()))
        self.assertIn("2026-07-01 14:00 UTC+02:00, with daylight saving", output.getvalue())

    def test_the_clock_is_written_unless_no_clock_is_given(self):
        self.assertTrue(tide_sync.options_from(tide_sync.parse_arguments([])).clock)
        self.assertFalse(tide_sync.options_from(tide_sync.parse_arguments(["--no-clock"])).clock)


class SyncTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.card = Path(self.directory.name)
        self.rippl = self.card / ".rippl"
        album = self.card / "Artist" / "Album"
        album.mkdir(parents=True)
        write_silent_flac(album / "01 - Song.flac", 1)
        (album / "cover.png").write_bytes(image_bytes((255, 0, 0)))
        internal = tempfile.TemporaryDirectory()
        self.addCleanup(internal.cleanup)
        self.internal = Path(internal.name)
        self.drives = {self.card: "SDCARD", self.internal: "RIPPL"}
        for name, replacement in (("mounted_roots", lambda: list(self.drives)),
                                  ("volume_label", lambda root: self.drives.get(root))):
            patcher = mock.patch.object(tide_sync, name, replacement)
            patcher.start()
            self.addCleanup(patcher.stop)

    def tearDown(self):
        self.directory.cleanup()

    def internal_drive(self):
        return tide_sync.InternalDrive(self.internal, tide_sync.detect_volume(self.internal))

    def run_sync(self, *arguments):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = tide_sync.main([str(self.card), *arguments])
        return status, output.getvalue()

    def prepare_rippl(self):
        self.rippl.mkdir()

    def library(self):
        card_store = tide_store.Store(self.rippl)
        region = card_store.region(0)
        return card_store.read(region.stream, region.offset, region.length)

    def cover_rows(self):
        card_store = tide_store.Store(self.rippl)
        region = card_store.region(2)
        data = card_store.read(region.stream, region.offset, region.length)
        return [struct.unpack_from("<II", data, offset) for offset in range(0, len(data), 8)]

    def expected_library(self, internal=None, excluded=frozenset()):
        return rippl_format.encode_library(library_index.build_library(
            *library_index.scan(self.card, internal, excluded)[:2]))

    def test_a_sync_writes_the_library_the_covers_and_the_fingerprint(self):
        self.prepare_rippl()
        status, output = self.run_sync()
        self.assertEqual(0, status, output)
        self.assertEqual(self.expected_library(), self.library())
        self.assertEqual([(0, self.cover_rows()[0][1])], self.cover_rows())
        self.assertTrue((self.rippl / "library" / "L0000.BIN").exists())
        self.assertTrue((self.rippl / "covers" / "C0000.BIN").exists())
        scan = (self.rippl / "scan.bin").read_bytes()
        self.assertEqual(4096, len(scan))
        volume = tide_sync.detect_volume(self.card)
        self.assertEqual(tide_sync.card_fingerprint(self.card, volume, self.internal_drive()),
                         tide_sync.stored_fingerprint(scan))
        self.assertIn(f"and the internal storage {self.internal}", output)
        self.assertIn("Eject the card safely", output.replace("eject", "Eject"))
        self.assertIn("sets its clock", output)
        self.assertIsNotNone(tide_sync.stored_clock((self.rippl / "clock.bin").read_bytes()))
        self.assertEqual(0, self.run_sync("--check")[0])

    def test_no_clock_leaves_the_clock_record_out(self):
        self.prepare_rippl()
        status, output = self.run_sync("--no-clock")
        self.assertEqual(0, status, output)
        self.assertFalse((self.rippl / "clock.bin").exists())
        self.assertNotIn("sets its clock", output)

    def test_covers_carry_the_players_stamp_and_are_kept_next_time(self):
        self.prepare_rippl()
        patcher = mock.patch.object(tide_sync, "detect_volume", lambda root: FIXED_UTC)
        patcher.start()
        self.addCleanup(patcher.stop)
        status, output = self.run_sync()
        self.assertEqual(0, status, output)
        art = cover_export.locate_art(self.card, ["Artist/Album/01 - Song.flac"])
        describe = tide_sync.art_describer(FIXED_UTC)
        stamp = cover_export.stamp_for(art, describe)
        self.assertEqual([(0, stamp)], self.cover_rows())
        status, output = self.run_sync()
        self.assertIn("1 cover (1 kept)", output)
        self.assertEqual([(0, stamp)], self.cover_rows())

    def test_cover_files_are_described_with_the_players_name_and_time(self):
        image = self.card / "Artist" / "Album" / "cover.png"
        describe = tide_sync.art_describer(FIXED_UTC)
        expected = tide_sync.card_stamp(image.stat(), FIXED_UTC)
        self.assertEqual((b"cover.png", expected), describe(image))
        unsupported = tide_sync.CardVolume("NTFS", False, False, 0, "")
        self.assertIsNone(tide_sync.art_describer(unsupported))

    def test_the_firmware_tag_in_scan_bin_is_kept(self):
        self.prepare_rippl()
        tag = b"0.1.0-abc".ljust(16, b"\0")
        (self.rippl / "scan.bin").write_bytes(SCAN_RECORD.pack(b"MESC", 1, 2, 3, tag) + bytes(4064))
        self.run_sync()
        scan = (self.rippl / "scan.bin").read_bytes()
        self.assertEqual(tag, scan[16:32])
        self.assertEqual(4096, len(scan))

    def test_a_dry_run_writes_nothing(self):
        self.prepare_rippl()
        status, output = self.run_sync("--dry-run")
        self.assertEqual(0, status, output)
        self.assertEqual([], sorted(path.name for path in self.rippl.iterdir()))
        self.assertIn("would write the library", output)

    def test_without_a_fingerprint_scan_bin_is_left_alone(self):
        self.prepare_rippl()
        record = SCAN_RECORD.pack(b"MESC", 1, 2, 3, bytes(16))
        (self.rippl / "scan.bin").write_bytes(record)
        self.run_sync("--no-fingerprint")
        self.assertEqual(record, (self.rippl / "scan.bin").read_bytes())
        self.assertTrue((self.rippl / "manifest.bin").exists())

    def test_no_covers_keeps_the_covers_the_card_has(self):
        self.prepare_rippl()
        self.run_sync()
        before = self.cover_rows()
        (self.card / "Artist" / "Album" / "cover.png").write_bytes(image_bytes((0, 0, 255)))
        status, output = self.run_sync("--no-covers")
        self.assertEqual(0, status, output)
        self.assertEqual(before, self.cover_rows())
        self.assertIn("Covers were kept as they were", output)

    def make_internal(self, internal):
        album = internal / "Band" / "Live"
        album.mkdir(parents=True)
        write_silent_flac(album / "01 - Tune.flac", 1)
        (album / "cover.png").write_bytes(image_bytes((0, 255, 0)))
        (internal / "HIFIEC90.IMG").write_bytes(bytes(4096))
        (internal / "HIFIEC90-not-for-install.bin").write_bytes(bytes(4096))
        return internal

    def test_the_internal_drive_is_found_and_both_drives_make_one_library(self):
        self.prepare_rippl()
        internal = self.make_internal(self.internal)
        status, output = self.run_sync()
        self.assertEqual(0, status, output)
        self.assertIn(f"and the internal storage {internal}", output)
        library = self.library()
        self.assertEqual(self.expected_library(internal), library)
        self.assertIn(b":internal/Band/Live/01 - Tune.flac", library)
        self.assertNotIn(b"HIFIEC", library)
        self.assertEqual(2, len([row for row in self.cover_rows() if row[0] != 0xFFFFFFFF]))
        self.assertEqual(tide_sync.card_fingerprint(self.card, tide_sync.detect_volume(self.card),
                                                    self.internal_drive()),
                         tide_sync.stored_fingerprint((self.rippl / "scan.bin").read_bytes()))
        self.assertIn("will use this library", output)
        self.assertEqual(0, self.run_sync("--check")[0])
        del self.drives[self.internal]
        self.assertEqual(1, self.run_sync("--check")[0])

    def test_internal_names_a_drive_with_another_name(self):
        self.prepare_rippl()
        del self.drives[self.internal]
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        internal = self.make_internal(Path(directory.name))
        status, output = self.run_sync("--internal", str(internal))
        self.assertEqual(0, status, output)
        self.assertIn(f"and the internal storage {internal.resolve()}", output)
        self.assertIn(b":internal/Band/Live/01 - Tune.flac", self.library())
        self.assertIn("will use this library", output)
        self.assertEqual(2, self.run_sync("--internal", str(self.card))[0])

    def test_without_the_internal_drive_the_card_is_synced_and_the_player_scans_again(self):
        self.prepare_rippl()
        del self.drives[self.internal]
        (self.rippl / "scan.bin").write_bytes(SCAN_RECORD.pack(b"MESC", 1, 2, 3, bytes(16)))
        status, output = self.run_sync()
        self.assertEqual(0, status, output)
        self.assertIn("(the drive called RIPPL) wasn't found", output)
        self.assertIn("scan the card once more", output)
        self.assertEqual(bytes(4), (self.rippl / "scan.bin").read_bytes()[:4])
        library = self.library()
        self.assertIn(b"Artist/Album/01 - Song.flac", library)
        self.assertNotIn(b":internal", library)

    def test_two_drives_called_rippl_need_internal(self):
        self.prepare_rippl()
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.drives[Path(directory.name)] = "RIPPL"
        status, output = self.run_sync()
        self.assertEqual(2, status, output)
        self.assertIn("More than one drive is called RIPPL", output)
        self.assertFalse((self.rippl / "manifest.bin").exists())

    def test_unticked_music_folders_are_left_out_of_the_library_and_the_fingerprint(self):
        self.prepare_rippl()
        spoken = self.card / "Talks" / "Episode"
        spoken.mkdir(parents=True)
        write_silent_flac(spoken / "01 - Talk.flac", 1)
        (self.rippl / "folders.bin").write_bytes(library_index.encode_excluded_folders(
            [library_index.folder_key(b"Talks")]) + bytes(4096 - 140))
        status, output = self.run_sync()
        self.assertEqual(0, status, output)
        self.assertIn("Leaving out 1 top-level folder", output)
        library = self.library()
        self.assertIn(b"Artist/Album/01 - Song.flac", library)
        self.assertNotIn(b"Talks", library)
        excluded = library_index.read_excluded_folders(self.rippl / "folders.bin")
        self.assertEqual(self.expected_library(None, excluded), library)
        volume = tide_sync.detect_volume(self.card)
        stored = tide_sync.stored_fingerprint((self.rippl / "scan.bin").read_bytes())
        drive = self.internal_drive()
        self.assertEqual(tide_sync.card_fingerprint(self.card, volume, drive, excluded), stored)
        self.assertNotEqual(tide_sync.card_fingerprint(self.card, volume, drive), stored)
        self.assertEqual(0, self.run_sync("--check")[0])
        write_silent_flac(spoken / "02 - More.flac", 1)
        self.assertEqual(0, self.run_sync("--check")[0])

    def test_a_folder_that_is_not_a_card_is_refused(self):
        status, output = self.run_sync()
        self.assertEqual(2, status)
        self.assertIn("isn't the top of a memory card", output)
        self.assertFalse(self.rippl.exists())


class AskForCardTest(unittest.TestCase):
    def test_dragged_and_quoted_paths_are_cleaned_up(self):
        self.assertEqual("E:\\Music", tide_sync.typed_path('  "E:\\Music" ', "win32"))
        self.assertEqual("/Volumes/MY CARD", tide_sync.typed_path("/Volumes/MY\\ CARD ", "darwin"))
        self.assertEqual("/Volumes/MY CARD", tide_sync.typed_path("'/Volumes/MY CARD'", "darwin"))

    def test_only_folders_with_a_rippl_folder_are_offered(self):
        with tempfile.TemporaryDirectory() as directory:
            player, other = Path(directory, "player"), Path(directory, "other")
            (player / ".rippl").mkdir(parents=True)
            other.mkdir()
            self.assertEqual([player], tide_sync.player_cards([other, player,
                                                               Path(directory, "missing")]))

    def test_one_known_card_is_the_default(self):
        card = Path("/Volumes/CARD")
        self.assertEqual(str(card), tide_sync.ask_for_card([card], read=lambda prompt: ""))
        self.assertEqual("/elsewhere",
                         tide_sync.ask_for_card([card], read=lambda prompt: "/elsewhere"))

    def test_an_empty_answer_without_a_known_card_writes_nothing(self):
        with self.assertRaises(tide_sync.SyncError):
            tide_sync.ask_for_card([], read=lambda prompt: " ")

    def test_the_card_can_be_left_out_of_the_command(self):
        self.assertIsNone(tide_sync.parse_arguments([]).card)


if __name__ == "__main__":
    unittest.main()

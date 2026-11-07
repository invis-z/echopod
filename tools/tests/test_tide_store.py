import struct
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import library_index
from tide import covers, records, rippl_format, store


def model_of(*tracks):
    return library_index.build_library([library_index.TrackInfo(*track) for track in tracks])


TRACKS = (
    ("B/Album/01 - One.flac", "One", "Band", "Band", "Album", "Rock", 2001, 1, 1, 1000),
    ("B/Album/02 - Two.flac", "Two", "Band", "Band", "Album", "Rock", 2001, 1, 2, 2000),
    ("A/Other/01 - Three.flac", "Three", "Act", "Act", "Other", "Pop", 1999, 1, 1, 3000),
)


class StoreTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.folder = Path(directory.name) / ".rippl"

    def test_a_commit_reads_back_after_reopening(self):
        card = store.Store(self.folder)
        self.assertFalse(card.found)
        data = bytes(range(256)) * 5000
        card.write(1, store.POOL_LIBRARY, data)
        card.set_region(0, 1, 0, len(data))
        card.set_value(3, 77)
        card.commit()
        reopened = store.Store(self.folder)
        self.assertTrue(reopened.found)
        self.assertEqual(1, reopened.committed.sequence)
        self.assertEqual(data, reopened.read(1))
        self.assertEqual(store.Region(1, 0, len(data)), reopened.region(0))
        self.assertEqual(77, reopened.value(3))
        self.assertEqual(2, reopened.committed.streams[1].count)

    def test_commits_alternate_slots_and_reuse_freed_segments(self):
        card = store.Store(self.folder)
        card.write(1, store.POOL_LIBRARY, b"first" * 1000)
        card.commit()
        card.write(1, store.POOL_LIBRARY, b"second" * 1000)
        card.commit()
        card.write(1, store.POOL_LIBRARY, b"third" * 1000)
        card.commit()
        reopened = store.Store(self.folder)
        self.assertEqual(3, reopened.committed.sequence)
        self.assertEqual(b"third" * 1000, reopened.read(1))
        self.assertEqual(2, reopened.committed.reserved[store.POOL_LIBRARY])

    def test_a_damaged_newer_copy_falls_back_to_the_older_one(self):
        card = store.Store(self.folder)
        card.write(1, store.POOL_LIBRARY, b"kept")
        card.commit()
        card.write(1, store.POOL_LIBRARY, b"lost")
        card.commit()
        manifest = self.folder / store.MANIFEST_NAME
        data = bytearray(manifest.read_bytes())
        data[store.SLOT_SIZE + 20] ^= 1
        manifest.write_bytes(bytes(data))
        self.assertEqual(b"kept", store.Store(self.folder).read(1))

    def test_a_dry_run_writes_nothing(self):
        card = store.Store(self.folder, dry_run=True)
        card.write(1, store.POOL_LIBRARY, b"data")
        self.assertEqual(store.POOL_SIZE[store.POOL_LIBRARY], card.new_bytes())
        card.commit()
        self.assertFalse(self.folder.exists())

    def test_the_manifest_has_the_players_size_and_checksum(self):
        state = store.State(sequence=5)
        encoded = store.encode_manifest(state, [store.NONE] * store.TOTAL_CAPACITY)
        self.assertEqual(store.BODY_SIZE + 4, len(encoded))
        self.assertLessEqual(len(encoded), store.SLOT_SIZE)
        self.assertEqual(5, store.decode_manifest(encoded)[0].sequence)
        self.assertIsNone(store.decode_manifest(encoded[:-1] + bytes([encoded[-1] ^ 1])))


class RipplFormatTest(unittest.TestCase):
    def test_the_header_lists_thirteen_sections(self):
        data = rippl_format.encode_library(model_of(*TRACKS))
        magic, version, count = struct.unpack_from("<4sHH", data)
        self.assertEqual((b"RLIB", 1, 13), (magic, version, count))

    def test_path_ids_are_sorted_and_fold_ascii_case(self):
        model = model_of(*TRACKS)
        data = rippl_format.encode_library(model)
        offset, count, size, _, _ = struct.unpack_from("<IIHHI", data, 8 + 10 * 16)
        rows = [struct.unpack_from("<QI", data, offset + index * size) for index in range(count)]
        self.assertEqual(sorted(rows), rows)
        for path_id, track in rows:
            self.assertEqual(rippl_format.path_id(model.tracks[track].path), path_id)
        self.assertEqual(rippl_format.path_id("a/b.FLAC"), rippl_format.path_id("A/B.flac"))

    def test_album_ranks_point_back_at_the_sorted_keys(self):
        model = model_of(*TRACKS)
        keys = rippl_format.ranked_keys(model)
        data = rippl_format.encode_library(model)
        offset, count, _, _, _ = struct.unpack_from("<IIHHI", data, 8 + 12 * 16)
        ranks = struct.unpack_from(f"<{count}I", data, offset)
        for album, rank in enumerate(ranks):
            self.assertEqual(album, keys[rank][1])


class RecordsTest(unittest.TestCase):
    def test_records_follow_the_players_part_wise_order(self):
        files = [(b"a b/x.flac", 1, 2, None), (b"a/z.flac", 3, 4, None), (b"a/b.flac", 5, 6, None)]
        data = records.encode_records(files)
        paths, offset = [], 0
        while offset < len(data):
            length = struct.unpack_from("<H", data, offset)[0]
            paths.append(data[offset + 21:data.index(b"\0", offset + 21)])
            offset += length
        self.assertEqual([b"a/b.flac", b"a/z.flac", b"a b/x.flac"], paths)

    def test_a_track_record_holds_its_fields(self):
        track = library_index.TrackInfo(*TRACKS[0], album_artist_guessed=True, chapters=3)
        data = records.encode_record(b"B/Album/01 - One.flac", 1234, 99, track)
        length, kind, flags, size, stamp, duration, year, disc, number, chapters = \
            records.RECORD_HEADER.unpack_from(data)
        self.assertEqual((len(data), 1, 1, 1234, 99, 1000, 2001, 1, 1, 3),
                         (length, kind, flags, size, stamp, duration, year, disc, number, chapters))
        self.assertEqual([b"B/Album/01 - One.flac", b"One", b"Band", b"Band", b"Album", b"Rock"],
                         data[21:-1].split(b"\0"))

    def test_a_record_too_long_for_the_player_is_left_out(self):
        self.assertEqual(b"", records.encode_record(b"x" * 5000, 0, 0, None))


class CoversTest(unittest.TestCase):
    def test_slots_fill_whole_records_of_each_segment(self):
        self.assertEqual(133, covers.PER_SEGMENT)
        self.assertEqual(132 * covers.RECORD_BYTES, covers.slot_offset(132))
        self.assertEqual(store.POOL_SIZE[store.POOL_COVERS], covers.slot_offset(133))


if __name__ == "__main__":
    unittest.main()

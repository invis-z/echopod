import struct
from dataclasses import dataclass, field

import cover_export
from tide import rippl_format, store

STREAM_TABLE = 0
STREAM_LIBRARY = 1
STREAM_COVERS = 4
STREAM_OTHER_COVERS = 5
REGION_LIBRARY = 0
REGION_TABLE = 2
VALUE_COVERS = 1
VALUE_SLOTS = 2
NO_COVER = 0xFFFFFFFF
RECORD_BYTES = cover_export.RECORD_BYTES
PER_SEGMENT = store.POOL_SIZE[store.POOL_COVERS] // RECORD_BYTES
ROW = struct.Struct("<II")
SECTION_ENTRY = struct.Struct("<IIHHI")
ALBUM_KEYS_SECTION = 11


@dataclass
class CoverResult:
    table: bytes = b""
    records: bytes = b""
    slots: int = 0
    covers: int = 0
    reused: int = 0
    missing: list = field(default_factory=list)


def slot_offset(slot):
    return slot // PER_SEGMENT * store.POOL_SIZE[store.POOL_COVERS] + slot % PER_SEGMENT * RECORD_BYTES


def old_keys(library):
    if len(library) < 8 or library[:4] != rippl_format.MAGIC:
        return []
    offset, count, size, _, _ = SECTION_ENTRY.unpack_from(library, 8 + ALBUM_KEYS_SECTION * 16)
    if size != rippl_format.KEY_RECORD.size or offset + count * size > len(library):
        return []
    return [rippl_format.KEY_RECORD.unpack_from(library, offset + index * size)[0]
            for index in range(count)]


class OldCovers:
    def __init__(self, card_store):
        self.store = card_store
        self.rows = {}
        self.stream = STREAM_COVERS
        try:
            self.load()
        except (store.StoreError, OSError, struct.error):
            self.rows = {}

    def load(self):
        library = self.store.region(REGION_LIBRARY)
        table = self.store.region(REGION_TABLE)
        stream = self.store.value(VALUE_COVERS)
        if not self.store.found or library.stream != STREAM_LIBRARY or table.stream != STREAM_TABLE:
            return
        if stream not in (STREAM_COVERS, STREAM_OTHER_COVERS):
            return
        self.stream = stream
        slots = self.store.value(VALUE_SLOTS)
        keys = old_keys(self.store.read(STREAM_LIBRARY, library.offset, library.length))
        data = self.store.read(STREAM_TABLE, table.offset, table.length)
        if len(data) != len(keys) * ROW.size:
            return
        for position, key in enumerate(keys):
            slot, stamp = ROW.unpack_from(data, position * ROW.size)
            if slot < slots and key not in self.rows:
                self.rows[key] = (slot, stamp)

    def record(self, key, stamp=None):
        found = self.rows.get(key)
        if found is None or (stamp is not None and (stamp == 0 or found[1] != stamp)):
            return None
        return self.store.read(self.stream, slot_offset(found[0]), RECORD_BYTES)



def append_record(result, record):
    offset = slot_offset(result.slots)
    if offset > len(result.records):
        result.records += bytes(offset - len(result.records))
    result.records += record
    result.slots += 1
    return result.slots - 1


def build(model, roots, old, describe=None, decode=True):
    result = CoverResult()
    sources = cover_export.album_sources(model)
    rows = bytearray()
    previous = None
    for key, album in rippl_format.ranked_keys(model):
        if previous is not None and key == previous[0]:
            rows += ROW.pack(*previous[1])
            continue
        row = (NO_COVER, 0)
        source = sources[album]
        if decode:
            art = cover_export.locate_art(roots, source.paths)
            stamp = 0 if art is None else cover_export.stamp_for(art, describe)
            kept = None if art is None else old.record(key, stamp)
            if kept is not None:
                result.reused += 1
                record = kept
            else:
                image = cover_export.converted(art, cover_export.unchanged)
                record = None if image is None else cover_export.cover_record(image)
            if record is not None:
                row = (append_record(result, record), stamp)
        else:
            kept = old.record(key)
            if kept is not None:
                result.reused += 1
                row = (append_record(result, kept), old.rows[key][1])
        if row[0] == NO_COVER:
            result.missing.append(f"{source.artist} - {source.title}")
        else:
            result.covers += 1
        rows += ROW.pack(*row)
        previous = (key, row)
    result.table = bytes(rows)
    return result


def other_stream(stream):
    return STREAM_OTHER_COVERS if stream == STREAM_COVERS else STREAM_COVERS


def stage(card_store, old, result):
    target = other_stream(old.stream)
    card_store.write(target, store.POOL_COVERS, result.records)
    card_store.write(old.stream, store.POOL_COVERS, b"")
    card_store.write(STREAM_TABLE, store.POOL_LIBRARY, result.table)
    card_store.set_region(REGION_TABLE, STREAM_TABLE, 0, len(result.table))
    card_store.set_value(VALUE_COVERS, target)
    card_store.set_value(VALUE_SLOTS, result.slots)

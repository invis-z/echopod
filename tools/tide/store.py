import os
import struct
from dataclasses import dataclass, field, replace
from pathlib import Path

MAGIC = 0x4D4C5052
VERSION = 1
POOL_LIBRARY = 0
POOL_COVERS = 1
POOL_SIZE = (1024 * 1024, 4 * 1024 * 1024)
POOL_CAPACITY = (512, 256)
POOL_BASE = (0, 512)
POOL_FOLDER = ("library", "covers")
POOL_PREFIX = ("L", "C")
TOTAL_CAPACITY = sum(POOL_CAPACITY)
STREAM_COUNT = 8
REGION_COUNT = 16
VALUE_COUNT = 8
SLOT_SIZE = 4096
MANIFEST_SIZE = 2 * SLOT_SIZE
MANIFEST_NAME = "manifest.bin"
NONE = 0xFFFF
FNV_OFFSET = 2166136261
FNV_PRIME = 16777619
WORD = 0xFFFFFFFF
HEAD = struct.Struct("<IHHI2H")
STREAM = struct.Struct("<BBHHI")
REGION = struct.Struct("<BII")
VALUES = struct.Struct(f"<{VALUE_COUNT}I")
NEXT = struct.Struct(f"<{TOTAL_CAPACITY}H")
CHECK = struct.Struct("<I")
BODY_SIZE = (HEAD.size + STREAM.size * STREAM_COUNT + REGION.size * REGION_COUNT + VALUES.size
             + NEXT.size)


class StoreError(Exception):
    pass


def fnv1a(data, value=FNV_OFFSET):
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & WORD
    return value


@dataclass(frozen=True)
class Stream:
    pool: int = POOL_LIBRARY
    head: int = NONE
    count: int = 0
    length: int = 0


@dataclass(frozen=True)
class Region:
    stream: int = 0
    offset: int = 0
    length: int = 0


@dataclass
class State:
    sequence: int = 0
    reserved: list = field(default_factory=lambda: [0, 0])
    streams: list = field(default_factory=lambda: [Stream() for _ in range(STREAM_COUNT)])
    regions: list = field(default_factory=lambda: [Region() for _ in range(REGION_COUNT)])
    values: list = field(default_factory=lambda: [0] * VALUE_COUNT)


def segment_name(pool, number):
    return f"{POOL_PREFIX[pool]}{number:04d}.BIN"


def encode_manifest(state, chains):
    body = HEAD.pack(MAGIC, VERSION, 0, state.sequence & WORD, *state.reserved)
    body += b"".join(STREAM.pack(s.pool, 0, s.head, s.count, s.length) for s in state.streams)
    body += b"".join(REGION.pack(r.stream, r.offset, r.length) for r in state.regions)
    body += VALUES.pack(*state.values) + NEXT.pack(*chains)
    return body + CHECK.pack(fnv1a(body))


def decode_manifest(data):
    if len(data) < BODY_SIZE + CHECK.size:
        return None
    body = data[:BODY_SIZE]
    if CHECK.unpack_from(data, BODY_SIZE)[0] != fnv1a(body):
        return None
    magic, version, _, sequence, *reserved = HEAD.unpack_from(body)
    if (magic, version) != (MAGIC, VERSION):
        return None
    offset = HEAD.size
    streams = []
    for _ in range(STREAM_COUNT):
        pool, _, head, count, length = STREAM.unpack_from(body, offset)
        streams.append(Stream(pool, head, count, length))
        offset += STREAM.size
    regions = []
    for _ in range(REGION_COUNT):
        regions.append(Region(*REGION.unpack_from(body, offset)))
        offset += REGION.size
    values = list(VALUES.unpack_from(body, offset))
    chains = list(NEXT.unpack_from(body, offset + VALUES.size))
    return State(sequence, list(reserved), streams, regions, values), chains


def newer(left, right):
    difference = (left - right) & WORD
    return difference != 0 and difference < 0x80000000


def stream_segments(stream, chains):
    numbers, number = [], stream.head
    for _ in range(stream.count):
        if number == NONE or number >= POOL_CAPACITY[stream.pool]:
            raise StoreError("a stream's segments are damaged")
        numbers.append(number)
        number = chains[POOL_BASE[stream.pool] + number]
    return numbers


class Store:
    def __init__(self, folder, dry_run=False):
        self.folder = Path(folder)
        self.dry_run = dry_run
        self.found = False
        self.slot = 1
        self.committed = State()
        self.chains = [NONE] * TOTAL_CAPACITY
        self.load()
        self.working = self.copy(self.committed)
        self.pending = {}

    @staticmethod
    def copy(state):
        return replace(state, reserved=list(state.reserved), streams=list(state.streams),
                       regions=list(state.regions), values=list(state.values))

    def load(self):
        try:
            data = (self.folder / MANIFEST_NAME).read_bytes()
        except OSError:
            return
        decoded = [decode_manifest(data[slot * SLOT_SIZE:(slot + 1) * SLOT_SIZE])
                   for slot in (0, 1)]
        if decoded[0] is None and decoded[1] is None:
            return
        second_first = decoded[1] is not None and (
            decoded[0] is None or newer(decoded[1][0].sequence, decoded[0][0].sequence))
        slot = 1 if second_first else 0
        self.committed, self.chains = decoded[slot]
        self.slot = slot
        self.found = True

    def segment_path(self, pool, number):
        return self.folder / POOL_FOLDER[pool] / segment_name(pool, number)

    def read(self, stream_index, offset=0, length=None):
        stream = self.committed.streams[stream_index]
        length = stream.length - offset if length is None else length
        if offset < 0 or length < 0 or offset + length > stream.length:
            raise StoreError("a read past the end of a stream")
        numbers = stream_segments(stream, self.chains)
        size = POOL_SIZE[stream.pool]
        parts = []
        while length > 0:
            within = offset % size
            take = min(size - within, length)
            with open(self.segment_path(stream.pool, numbers[offset // size]), "rb") as file:
                file.seek(within)
                part = file.read(take)
            if len(part) != take:
                raise StoreError("a segment file is shorter than its stream")
            parts.append(part)
            offset += take
            length -= take
        return b"".join(parts)

    def region(self, index):
        return self.committed.regions[index]

    def value(self, index):
        return self.committed.values[index]

    def keep(self, stream_index):
        self.working.streams[stream_index] = self.committed.streams[stream_index]
        self.pending.pop(stream_index, None)

    def write(self, stream_index, pool, data):
        self.working.streams[stream_index] = Stream(pool)
        self.pending[stream_index] = (pool, bytes(data))

    def set_region(self, index, stream, offset, length):
        self.working.regions[index] = Region(stream, offset, length)

    def set_value(self, index, value):
        self.working.values[index] = value

    def in_use(self):
        used = set()
        for state in (self.committed, self.working):
            for index, stream in enumerate(state.streams):
                if state is self.working and index in self.pending:
                    continue
                used.update((stream.pool, number)
                            for number in stream_segments(stream, self.chains))
        return used

    def allocate(self, used, pool, needed, created):
        numbers = []
        for number in range(self.working.reserved[pool]):
            if len(numbers) == needed:
                break
            if (pool, number) not in used:
                numbers.append(number)
        while len(numbers) < needed:
            number = self.working.reserved[pool]
            if number >= POOL_CAPACITY[pool]:
                raise StoreError("the card holds no more segment files")
            self.working.reserved[pool] = number + 1
            created.append((pool, number))
            numbers.append(number)
        used.update((pool, number) for number in numbers)
        return numbers

    def plan(self):
        used = self.in_use()
        created, placed = [], {}
        chains = list(self.chains)
        for index, (pool, data) in sorted(self.pending.items()):
            size = POOL_SIZE[pool]
            numbers = self.allocate(used, pool, -(-len(data) // size), created)
            for first, second in zip(numbers, numbers[1:] + [NONE]):
                chains[POOL_BASE[pool] + first] = second
            head = numbers[0] if numbers else NONE
            self.working.streams[index] = Stream(pool, head, len(numbers), len(data))
            placed[index] = numbers
        return created, placed, chains

    def new_bytes(self):
        return sum(POOL_SIZE[pool] for pool, _ in self.plan()[0])

    def write_segment(self, pool, number, offset, data, create):
        path = self.segment_path(pool, number)
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "r+b" if path.exists() and not create else "w+b") as file:
            if create or file.seek(0, os.SEEK_END) < POOL_SIZE[pool]:
                file.truncate(POOL_SIZE[pool])
            file.seek(offset)
            file.write(data)
            file.flush()
            os.fsync(file.fileno())

    def commit(self):
        created, placed, chains = self.plan()
        self.working.sequence = (self.committed.sequence + 1) & WORD
        if self.dry_run:
            return created
        created_set = set(created)
        for index, (pool, data) in sorted(self.pending.items()):
            size = POOL_SIZE[pool]
            for position, number in enumerate(placed[index]):
                part = data[position * size:(position + 1) * size]
                self.write_segment(pool, number, 0, part, (pool, number) in created_set)
        for pool, number in created:
            if not any(number in placed[index] for index, (p, _) in self.pending.items()
                       if p == pool):
                self.write_segment(pool, number, 0, b"", True)
        manifest = self.folder / MANIFEST_NAME
        manifest.parent.mkdir(parents=True, exist_ok=True)
        slot = self.slot ^ 1
        with open(manifest, "r+b" if manifest.exists() else "w+b") as file:
            if file.seek(0, os.SEEK_END) < MANIFEST_SIZE:
                file.truncate(MANIFEST_SIZE)
            file.seek(slot * SLOT_SIZE)
            file.write(encode_manifest(self.working, chains))
            file.flush()
            os.fsync(file.fileno())
        self.committed, self.chains, self.slot, self.found = self.working, chains, slot, True
        self.working = self.copy(self.committed)
        self.pending = {}
        return created

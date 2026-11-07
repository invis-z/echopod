import struct

RECORD_HEADER = struct.Struct("<HBBIIIHBB")
RECORD_MAX = 4096
TRACK = 1
SKIPPED = 2
GUESSED = 1


def part_order(path):
    return tuple(path.split(b"/"))


def text(value):
    return value.encode("utf-8", "surrogateescape") + b"\0"


def encode_record(path, size, stamp, track):
    texts = path + b"\0"
    if track is not None:
        texts += b"".join(text(value) for value in (track.title, track.artist, track.album_artist,
                                                       track.album, track.genre))
    length = RECORD_HEADER.size + len(texts)
    if length > RECORD_MAX:
        return b""
    if track is None:
        header = RECORD_HEADER.pack(length, SKIPPED, 0, size, stamp, 0, 0, 0, 0)
    else:
        header = RECORD_HEADER.pack(length, TRACK, GUESSED if track.album_artist_guessed else 0,
                                    size, stamp, track.duration_ms, min(track.year, 0xFFFF),
                                    track.disc, track.number)
    return header + texts


def encode_records(files):
    ordered = sorted(files, key=lambda item: part_order(item[0]))
    return b"".join(encode_record(*item) for item in ordered)

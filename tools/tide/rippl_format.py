import struct

import library_index

MAGIC = b"RLIB"
VERSION = 1
HEADER_SIZE = 256
ALIGNMENT = 4
NONE = 0xFFFFFFFF
KEY_SEPARATOR = b"\x1f"
FNV64_OFFSET = 14695981039346656037
FNV64_PRIME = 1099511628211
MASK64 = (1 << 64) - 1

HEADER_PREFIX = struct.Struct("<4sHH")
SECTION_ENTRY = struct.Struct("<IIHHI")
TRACK_RECORD = struct.Struct("<IIIIIIIBBH")
ALBUM_RECORD = struct.Struct("<IIIIIHH")
GROUP_RECORD = struct.Struct("<III")
FOLDER_RECORD = struct.Struct("<IIIII")
INDEX_RECORD = struct.Struct("<I")
KEY_RECORD = struct.Struct("<QI")


def fnv1a64(data, value=FNV64_OFFSET):
    for byte in data:
        value = ((value ^ byte) * FNV64_PRIME) & MASK64
    return value


def folded(text):
    return bytes(byte + 32 if 65 <= byte <= 90 else byte for byte in text.encode("utf-8"))


def path_id(path):
    return fnv1a64(folded(path))


def album_key(album_artist, title):
    return fnv1a64(title.encode("utf-8"),
                   fnv1a64(KEY_SEPARATOR, fnv1a64(album_artist.encode("utf-8"))))


def album_keys(model):
    return [album_key(model.album_artists[album.album_artist].name, album.title)
            for album in model.albums]


def ranked_keys(model):
    return sorted((key, album) for album, key in enumerate(album_keys(model)))


def encode_tracks(model, strings):
    return b"".join(
        TRACK_RECORD.pack(strings.add(track.title), strings.add(track.path),
                          model.track_albums[position], model.track_artists[position],
                          model.track_genres[position], model.track_folders[position],
                          track.duration_ms, track.disc, track.number, 0)
        for position, track in enumerate(model.tracks))


def encode_albums(model, strings):
    return b"".join(
        ALBUM_RECORD.pack(strings.add(album.title), album.album_artist, album.genre,
                          album.first_track, album.track_count, min(album.year, 0xFFFF), 0)
        for album in model.albums)


def encode_groups(groups, strings, links):
    data = bytearray()
    for group in groups:
        data += GROUP_RECORD.pack(strings.add(group.name), len(links), len(group.albums))
        links.extend(group.albums)
    return bytes(data)


def encode_folders(folders, strings, links):
    data = bytearray()
    for folder in folders:
        parent = NONE if folder.parent == library_index.NONE else folder.parent
        data += FOLDER_RECORD.pack(strings.add(folder.name), parent, len(folder.subfolders),
                                   len(links), len(folder.tracks))
        links.extend(folder.subfolders + folder.tracks)
    return bytes(data)


def encode_playlists(playlists, strings, links):
    data = bytearray()
    for playlist in playlists:
        data += GROUP_RECORD.pack(strings.add(playlist.name), len(links), len(playlist.tracks))
        links.extend(playlist.tracks)
    return bytes(data)


def encode_indices(values):
    return b"".join(INDEX_RECORD.pack(value) for value in values)


def encode_sections(model):
    strings = library_index.StringTable()
    links = []
    sections = [
        (encode_tracks(model, strings), len(model.tracks), TRACK_RECORD.size, model.music_tracks),
        (encode_albums(model, strings), len(model.albums), ALBUM_RECORD.size, model.music_albums),
        (encode_groups(model.artists, strings, links), len(model.artists), GROUP_RECORD.size,
         model.music_artists),
        (encode_groups(model.album_artists, strings, links), len(model.album_artists),
         GROUP_RECORD.size, model.music_album_artists),
        (encode_groups(model.genres, strings, links), len(model.genres), GROUP_RECORD.size,
         model.music_genres),
        (encode_folders(model.folders, strings, links), len(model.folders), FOLDER_RECORD.size,
         0),
        (encode_indices(model.title_order), len(model.title_order), INDEX_RECORD.size,
         model.music_tracks),
        (encode_playlists(model.playlists, strings, links), len(model.playlists),
         GROUP_RECORD.size, 0),
    ]
    sections.append((encode_indices(links), len(links), INDEX_RECORD.size, 0))
    sections.append((bytes(strings.blob), len(strings.blob), 1, 0))
    ids = sorted((path_id(track.path), position) for position, track in enumerate(model.tracks))
    sections.append((b"".join(KEY_RECORD.pack(*row) for row in ids), len(ids), KEY_RECORD.size, 0))
    keys = ranked_keys(model)
    sections.append((b"".join(KEY_RECORD.pack(*row) for row in keys), len(keys), KEY_RECORD.size,
                     0))
    ranks = [0] * len(keys)
    for rank, (_, album) in enumerate(keys):
        ranks[album] = rank
    sections.append((encode_indices(ranks), len(ranks), INDEX_RECORD.size, 0))
    return sections


def encode_library(model):
    sections = encode_sections(model)
    header = bytearray(HEADER_PREFIX.pack(MAGIC, VERSION, len(sections)))
    body = bytearray()
    for data, count, record_size, music in sections:
        body += bytes(-len(body) % ALIGNMENT)
        header += SECTION_ENTRY.pack(HEADER_SIZE + len(body), count, record_size, 0, music)
        body += data
    return bytes(header) + bytes(HEADER_SIZE - len(header)) + bytes(body)

import base64
import io
import posixpath
import struct
from dataclasses import dataclass, field
from pathlib import Path


FULL_SIZE = 112
HALF_SIZE = 56
FNV_OFFSET = 2166136261
FNV_PRIME = 16777619
FRONT_COVER = 3
OTHER_PICTURE = 0
APE_COVER_PREFIX = "cover art ("
APE_FRONT_KEY = "cover art (front)"
ASF_PICTURE_HEAD = struct.Struct("<BI")
FOLDER_IMAGE_STEMS = ("cover", "folder", "front", "albumart")
FOLDER_IMAGE_SUFFIXES = (".jpg", ".jpeg", ".png")
STAMP_NUMBERS = struct.Struct("<III")
RECORD_BYTES = (FULL_SIZE * FULL_SIZE + HALF_SIZE * HALF_SIZE) * 2
EMBEDDED_ART = 1
FILE_ART = 2
WORD_MASK = 0xFFFFFFFF
UNKNOWN_STAMP = 0
ART_TRACKS = 4
OGG_PICTURE_KEY = "metadata_block_picture"


@dataclass
class AlbumSource:
    title: str
    artist: str
    paths: list = field(default_factory=list)


@dataclass(frozen=True)
class Art:
    data: bytes
    file: Path
    relative: str
    picture_length: int = None


def fnv1a(data, start=FNV_OFFSET):
    value = start
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & 0xFFFFFFFF
    return value


def rgb565(red, green, blue):
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def square_crop(image):
    width, height = image.size
    side = min(width, height)
    left, top = (width - side) // 2, (height - side) // 2
    return image.crop((left, top, left + side, top + side))


def cover_pixels(image, size):
    from PIL import Image
    resized = square_crop(image.convert("RGB")).resize((size, size), Image.Resampling.LANCZOS)
    return [rgb565(*pixel) for pixel in resized.getdata()]


def mp4_pictures(tags):
    return [(FRONT_COVER, bytes(cover)) for cover in tags.get("covr", [])]


def ape_picture(key, data):
    name_end = data.find(b"\x00")
    if name_end < 0:
        return None
    kind = FRONT_COVER if key.lower() == APE_FRONT_KEY else OTHER_PICTURE
    return kind, data[name_end + 1:]


def ape_pictures(tags):
    from mutagen.apev2 import BINARY
    pictures = []
    for key, value in tags.items():
        if (len(key) > len(APE_COVER_PREFIX) and key.lower().startswith(APE_COVER_PREFIX)
                and value.kind == BINARY):
            picture = ape_picture(key, value.value)
            if picture is not None:
                pictures.append(picture)
    return pictures


def skip_utf16_string(data, cursor):
    while cursor + 2 <= len(data):
        unit = data[cursor:cursor + 2]
        cursor += 2
        if unit == b"\x00\x00":
            break
    return cursor


def asf_picture(data):
    if len(data) < ASF_PICTURE_HEAD.size:
        return None
    kind, length = ASF_PICTURE_HEAD.unpack_from(data)
    cursor = skip_utf16_string(data, skip_utf16_string(data, ASF_PICTURE_HEAD.size))
    if cursor > len(data) or length > len(data) - cursor:
        return None
    return kind, data[cursor:cursor + length]


def asf_pictures(tags):
    pictures = []
    for attribute in tags.get("WM/Picture", []):
        picture = asf_picture(bytes(attribute.value)) if isinstance(attribute.value, bytes) else None
        if picture is not None:
            pictures.append(picture)
    return pictures


def tag_pictures(tags):
    from mutagen.apev2 import APEv2
    from mutagen.asf import ASFTags
    from mutagen.mp4 import MP4Tags
    if isinstance(tags, MP4Tags):
        return mp4_pictures(tags)
    if isinstance(tags, APEv2):
        return ape_pictures(tags)
    if isinstance(tags, ASFTags):
        return asf_pictures(tags)
    if hasattr(tags, "getall"):
        return [(frame.type, frame.data) for frame in tags.getall("APIC")]
    return []


def ogg_picture(value):
    from mutagen import MutagenError
    from mutagen.flac import Picture
    try:
        picture = Picture(base64.b64decode(value, validate=True))
    except (ValueError, TypeError, struct.error, MutagenError):
        return None
    return picture.type, picture.data


def ogg_pictures(tags):
    pictures = (ogg_picture(value) for value in tags.get(OGG_PICTURE_KEY, []))
    return [picture for picture in pictures if picture is not None]


def embedded_pictures(path):
    from mutagen import File, MutagenError
    from mutagen.oggopus import OggOpus
    from mutagen.oggvorbis import OggVorbis
    try:
        audio = File(path)
    except (MutagenError, OSError):
        return []
    if audio is None:
        return []
    pictures = [(picture.type, picture.data) for picture in getattr(audio, "pictures", [])]
    tags = getattr(audio, "tags", None)
    if tags is not None and isinstance(audio, (OggOpus, OggVorbis)):
        pictures += ogg_pictures(tags)
    elif tags is not None:
        pictures += tag_pictures(tags)
    return pictures


def name_order(path):
    return path.name.encode("utf-8", "surrogateescape")


def folder_image(folder):
    try:
        candidates = sorted((path for path in folder.iterdir() if path.is_file()), key=name_order)
    except OSError:
        return None
    for stem in FOLDER_IMAGE_STEMS:
        for path in candidates:
            if path.stem.lower() == stem and path.suffix.lower() in FOLDER_IMAGE_SUFFIXES:
                return path
    return None


def chosen_picture(pictures):
    for picture_type, data in pictures:
        if picture_type == FRONT_COVER:
            return data
    return pictures[0][1] if pictures else None


def folder_art(root, relative_paths):
    for folder in dict.fromkeys(posixpath.dirname(relative) for relative in relative_paths):
        image = folder_image(root / folder)
        if image is not None:
            try:
                return Art(image.read_bytes(), image, posixpath.join(folder, image.name))
            except OSError:
                return None
    return None


def locate_art(root, relative_paths):
    for relative in relative_paths[:ART_TRACKS]:
        picture = chosen_picture(embedded_pictures(root / relative))
        if picture:
            return Art(picture, root / relative, relative, len(picture))
    return folder_art(root, relative_paths)


def find_art(root, relative_paths):
    art = locate_art(root, relative_paths)
    return None if art is None else art.data


def art_stamp(card_path, first, second, third):
    numbers = STAMP_NUMBERS.pack(first & WORD_MASK, second & WORD_MASK, third & WORD_MASK)
    return fnv1a(numbers, fnv1a(card_path))


def file_card_path(art, name):
    folder = posixpath.dirname(art.relative).encode("utf-8")
    return b"/" + (folder + b"/" if folder else b"") + name


def stamp_for(art, describe=None):
    try:
        size = art.file.stat().st_size
    except OSError:
        return UNKNOWN_STAMP
    if art.picture_length is not None:
        card_path = b"/" + art.relative.encode("utf-8")
        return art_stamp(card_path, size, art.picture_length, EMBEDDED_ART)
    try:
        described = None if describe is None else describe(art.file)
    except OSError:
        described = None
    if described is None:
        return UNKNOWN_STAMP
    name, modified = described
    return art_stamp(file_card_path(art, name), size, modified, FILE_ART)


def album_sources(model):
    sources = []
    for album in model.albums:
        artist = model.album_artists[album.album_artist].name
        sources.append(AlbumSource(album.title, artist))
    for track, album_index in zip(model.tracks, model.track_albums):
        sources[album_index].paths.append(track.path)
    return sources


def pack_pixels(pixels):
    return struct.pack(f"<{len(pixels)}H", *pixels)


def cover_record(image):
    return pack_pixels(cover_pixels(image, FULL_SIZE)) + pack_pixels(cover_pixels(image, HALF_SIZE))


def load_image(data):
    from PIL import Image
    image = Image.open(io.BytesIO(data))
    image.load()
    return image


def unchanged(image):
    return image


def converted(art, convert):
    try:
        return None if art is None else convert(load_image(art.data))
    except Exception:
        return None

import argparse
import importlib.util
import os
import re
import string
import struct
import subprocess
import sys
import time
from dataclasses import dataclass, replace
from datetime import datetime, timedelta
from pathlib import Path

import cover_export
import library_index
from tide import covers as tide_covers
from tide import daylight, records, rippl_format, store

RIPPL_FOLDER = ".rippl"
MEBIBYTE = 1024 * 1024
WRITE_CHUNK_BYTES = MEBIBYTE
SCAN_MAGIC = b"MESC"
SCAN_RECORD = struct.Struct("<4sIII16s")
SCAN_PRINT_BYTES = 16
CLOCK_NAME = "clock.bin"
CLOCK_MAGIC = b"ECLK"
CLOCK_VERSION = 1
CLOCK_HAS_RULE = 1
CLOCK_RECORD = struct.Struct("<4sBBHIhh3BxhBBBxhI")
CLOCK_CHECKED_BYTES = 28
CLOCK_FIRST_YEAR = 2024
CLOCK_LAST_YEAR = 2099
WORD_MASK = 0xFFFFFFFF
SECOND_SEED = cover_export.FNV_OFFSET ^ 0x5BD1E995
PATH_CAPACITY = 1024
MAX_DEPTH = 32
EXFAT_SIZE_LIMIT = 1 << 32
COUNTED_SUFFIXES = tuple(sorted(suffix.encode("ascii") for suffix in
                                library_index.AUDIO_SUFFIXES | library_index.PLAYLIST_SUFFIXES))
ART_NAMES = frozenset(f"{stem}{suffix}".encode("ascii")
                      for stem in cover_export.FOLDER_IMAGE_STEMS
                      for suffix in cover_export.FOLDER_IMAGE_SUFFIXES)
SYSTEM_FOLDER = b"system volume information"
INTERNAL_NAME = library_index.INTERNAL_FOLDER.encode("ascii")
INTERNAL_LABEL = "RIPPL"
UNIX_EPOCH = datetime(1970, 1, 1)
DOS_FIRST_YEAR = 1980
DOS_LAST_YEAR = 2107
NANOSECONDS = 1_000_000_000
FAT_NAMES = {"fat", "fat12", "fat16", "fat32", "vfat", "msdos"}
EXFAT_NAMES = {"exfat"}
DRIVE_LETTER = re.compile(r"^[A-Za-z]:$")
SEM_FAILCRITICALERRORS = 0x0001
MAC_MOUNT_LINE = re.compile(r"^.* on (.+) \(([^,)]+)(?:, ([^)]*))?\)$")
MOUNT_ESCAPE = re.compile(r"\\([0-7]{3})")
REQUIREMENTS = Path(__file__).resolve().with_name("requirements.txt")
MAC_CAVEAT = ("on a Mac the player's file times are a best guess; after the player has scanned "
              "the card, --check shows whether they match")
LINUX_CAVEAT = ("on Linux the card's file times are read as UTC (or the mount's time_offset); "
                "after the player has scanned the card, --check shows whether they match")
OTHER_CAVEAT = "this isn't a FAT32 or exFAT card, so the file times may not match the player's"


@dataclass(frozen=True)
class CardFile:
    name: str
    standard_size: int
    header_size: int


SCAN_FILE = CardFile("scan.bin", 4096, SCAN_RECORD.size)


@dataclass(frozen=True)
class CardEntry:
    name: bytes
    directory: bool
    size: int
    modified: int
    location: object = None


@dataclass(frozen=True)
class Fingerprint:
    complete: bool
    first: int
    second: int
    entries: int


@dataclass(frozen=True)
class CardVolume:
    file_system: str
    supported: bool
    exfat: bool
    fixed_offset: int
    caveat: str


@dataclass(frozen=True)
class InternalDrive:
    root: Path
    volume: CardVolume


STREAM_LIBRARY = 1
STREAM_RECORDS = 2
STREAM_OTHER_RECORDS = 3
REGION_LIBRARY = 0
VALUE_RECORDS = 0


@dataclass
class SyncOptions:
    covers: bool = True
    fingerprint: bool = True
    clock: bool = True
    dry_run: bool = False
    verbose: bool = False


class SyncError(Exception):
    pass


class FingerprintHash:
    def __init__(self):
        self.first = cover_export.FNV_OFFSET
        self.second = SECOND_SEED
        self.entries = 0

    def mix(self, data):
        self.first = cover_export.fnv1a(data, self.first)
        self.second = cover_export.fnv1a(data, self.second)

    def mix_entry(self, folder, entry):
        self.mix(folder + b"/" + entry.name + struct.pack(
            "<III", 1 if entry.directory else 0, entry.size & WORD_MASK,
            entry.modified & WORD_MASK))
        self.entries += 1


def fingerprint_counts(name, directory):
    folded = name.lower()
    if directory:
        return folded != SYSTEM_FOLDER
    return folded in ART_NAMES or any(
        len(folded) > len(suffix) and folded.endswith(suffix) for suffix in COUNTED_SUFFIXES)


def left_out(folder, name, excluded):
    if not excluded:
        return False
    if folder == b"/":
        return library_index.folder_key(name) in excluded
    return folder == b"/" + INTERNAL_NAME and library_index.folder_key(name, True) in excluded


def child_path(folder, name):
    return (b"" if folder == b"/" else folder) + b"/" + name


def walk_folder(state, list_folder, folder, entries, depth, excluded=frozenset()):
    for entry in entries:
        if entry.name.startswith(b".") or not fingerprint_counts(entry.name, entry.directory):
            continue
        if entry.directory and left_out(folder, entry.name, excluded):
            continue
        state.mix_entry(folder, entry)
        if not entry.directory:
            continue
        path = child_path(folder, entry.name)
        if depth + 1 >= MAX_DEPTH or len(path) + 1 > PATH_CAPACITY:
            return False
        listing = list_folder(entry.location)
        if listing is not None and not walk_folder(state, list_folder, path, listing, depth + 1,
                                                   excluded):
            return False
    return True


def music_fingerprint(list_folder, root_location, excluded=frozenset()):
    state = FingerprintHash()
    listing = list_folder(root_location)
    complete = listing is not None and walk_folder(state, list_folder, b"/", listing, 0, excluded)
    return Fingerprint(complete, state.first, state.second, state.entries)


def dos_stamp(local_seconds):
    moment = UNIX_EPOCH + timedelta(seconds=local_seconds)
    if not DOS_FIRST_YEAR <= moment.year <= DOS_LAST_YEAR:
        return 0
    date = (moment.year - DOS_FIRST_YEAR) << 9 | moment.month << 5 | moment.day
    clock = moment.hour << 11 | moment.minute << 5 | moment.second // 2
    return date << 16 | clock


def current_offset():
    return time.localtime().tm_gmtoff


def card_name(name, platform=sys.platform):
    return library_index.name_bytes(name, platform)


def card_stamp(stat, volume):
    seconds = stat.st_mtime_ns // NANOSECONDS
    if volume.exfat:
        return seconds // 2 & WORD_MASK
    return dos_stamp(seconds + volume.fixed_offset)


def host_entry(entry, volume):
    stat = entry.stat(follow_symlinks=False)
    directory = entry.is_dir(follow_symlinks=False)
    if volume.exfat and not directory and stat.st_size >= EXFAT_SIZE_LIMIT:
        return None
    return CardEntry(card_name(entry.name), directory, 0 if directory else stat.st_size,
                     card_stamp(stat, volume), entry.path)


def host_lister(volume):
    def list_folder(location):
        try:
            with os.scandir(location) as listing:
                found = [entry for entry in listing if not entry.name.startswith(".")]
        except OSError:
            return None
        entries = (host_entry(entry, volume) for entry in found)
        return [entry for entry in entries if entry is not None]
    return list_folder


def drives_lister(card, volume, internal):
    card_lister = host_lister(volume)
    if internal is None:
        return card_lister
    internal_lister = host_lister(internal.volume)
    top = str(card)

    def list_folder(location):
        if isinstance(location, tuple):
            listing = internal_lister(location[0])
            return None if listing is None else [replace(entry, location=(entry.location,))
                                                 for entry in listing]
        listing = card_lister(location)
        if listing is not None and location == top:
            listing.append(CardEntry(INTERNAL_NAME, True, 0, 0, (str(internal.root),)))
        return listing
    return list_folder


def card_fingerprint(card, volume, internal=None, excluded=frozenset()):
    return music_fingerprint(drives_lister(card, volume, internal), str(card), excluded)


def scan_record(fingerprint, version):
    return SCAN_RECORD.pack(SCAN_MAGIC, fingerprint.first, fingerprint.second,
                            fingerprint.entries, version)


def stored_fingerprint(data):
    if len(data) < SCAN_RECORD.size:
        return None
    magic, first, second, entries, _ = SCAN_RECORD.unpack_from(data)
    return Fingerprint(True, first, second, entries) if magic == SCAN_MAGIC else None


def edge_fields(edge):
    return edge.month, edge.week, edge.weekday, edge.minutes


def clock_record(utc_seconds, standard_minutes, rule=None):
    flags = CLOCK_HAS_RULE if rule is not None else 0
    save = rule.save_minutes if rule is not None else 0
    start = edge_fields(rule.start) if rule is not None else (0, 0, 0, 0)
    end = edge_fields(rule.end) if rule is not None else (0, 0, 0, 0)
    body = CLOCK_RECORD.pack(CLOCK_MAGIC, CLOCK_VERSION, flags, 0, int(utc_seconds),
                             int(standard_minutes), save, *start, *end, 0)[:CLOCK_CHECKED_BYTES]
    return body + struct.pack("<I", cover_export.fnv1a(body, cover_export.FNV_OFFSET))


def stored_clock(data):
    if len(data) < CLOCK_RECORD.size:
        return None
    fields = CLOCK_RECORD.unpack_from(data)
    magic, version, flags, _, utc_seconds, standard_minutes, save = fields[:7]
    body = bytes(data[:CLOCK_CHECKED_BYTES])
    if (magic, version) != (CLOCK_MAGIC, CLOCK_VERSION):
        return None
    if fields[-1] != cover_export.fnv1a(body, cover_export.FNV_OFFSET):
        return None
    rule = None
    if flags & CLOCK_HAS_RULE:
        rule = daylight.Rule(save, daylight.Edge(*fields[7:11]), daylight.Edge(*fields[11:15]))
    return utc_seconds, standard_minutes, rule


def clock_text(utc_seconds, offset_minutes, rule=None):
    local = UNIX_EPOCH + timedelta(seconds=utc_seconds + offset_minutes * 60)
    sign = "-" if offset_minutes < 0 else "+"
    hours, minutes = divmod(abs(offset_minutes), 60)
    text = f"{local:%Y-%m-%d %H:%M} UTC{sign}{hours:02d}:{minutes:02d}"
    return text + (", with daylight saving" if rule is not None else "")


def write_clock(rippl, options, now=None, offset_at=daylight.local_offset_minutes):
    utc_seconds = int(time.time()) if now is None else int(now)
    offset_minutes = offset_at(utc_seconds)
    local_year = (UNIX_EPOCH + timedelta(seconds=utc_seconds + offset_minutes * 60)).year
    if not CLOCK_FIRST_YEAR <= local_year <= CLOCK_LAST_YEAR:
        print(f"  {CLOCK_NAME:<11} not written: this computer's clock says {local_year}")
        return False
    try:
        standard_minutes, rule = daylight.computer_zone(utc_seconds, offset_at)
    except (OSError, OverflowError, ValueError):
        standard_minutes, rule = offset_minutes, None
    text = clock_text(utc_seconds, offset_minutes, rule)
    if options.dry_run:
        print(f"  would write {CLOCK_NAME}: {text}")
        return False
    try:
        (rippl / CLOCK_NAME).write_bytes(clock_record(utc_seconds, standard_minutes, rule))
    except OSError as error:
        raise SyncError(f"couldn't write {rippl / CLOCK_NAME}: {error.strerror or error}") from error
    print(f"  {CLOCK_NAME:<11} {text}")
    return True


def stored_version(data):
    if len(data) < SCAN_RECORD.size:
        return bytes(SCAN_RECORD.size - SCAN_PRINT_BYTES)
    return bytes(data[SCAN_PRINT_BYTES:SCAN_RECORD.size])


def read_prefix(path, length):
    try:
        with open(path, "rb") as file:
            return file.read(length)
    except OSError:
        return b""


def target_size(existing, needed, standard):
    if existing is not None and existing >= needed:
        return existing
    return max(standard, needed)


def existing_size(path):
    try:
        return path.stat().st_size
    except FileNotFoundError:
        return None


def zero_fill(file, start, end):
    file.seek(start)
    while start < end:
        length = min(WRITE_CHUNK_BYTES, end - start)
        file.write(bytes(length))
        start += length


def settle(file):
    file.flush()
    os.fsync(file.fileno())


def write_card_file(path, data, card_file):
    size = target_size(existing_size(path), len(data), card_file.standard_size)
    header = min(card_file.header_size, len(data))
    with open(path, "r+b" if path.exists() else "w+b") as file:
        file.write(bytes(header))
        settle(file)
        file.seek(header)
        file.write(data[header:])
        end = file.seek(0, os.SEEK_END)
        zero_fill(file, end, size)
        settle(file)
        file.seek(0)
        file.write(data[:header])
        settle(file)
    return size


def forget_fingerprint(path):
    if existing_size(path):
        with open(path, "r+b") as file:
            file.write(bytes(len(SCAN_MAGIC)))
            settle(file)


def file_system_name(card):
    if sys.platform == "win32":
        return windows_file_system(card)
    mounts = linux_mounts() if sys.platform.startswith("linux") else mac_mounts()
    mount = mount_for(card, mounts)
    return (mount[1], mount[2]) if mount else (None, "")


def windows_file_system(card):
    import ctypes
    name = ctypes.create_unicode_buffer(64)
    found = ctypes.windll.kernel32.GetVolumeInformationW(
        ctypes.c_wchar_p(card.anchor), None, 0, None, None, None, name, len(name))
    return (name.value if found else None), ""


def linux_mounts():
    try:
        lines = Path("/proc/self/mounts").read_text(encoding="utf-8",
                                                    errors="surrogateescape").splitlines()
    except OSError:
        return []
    mounts = []
    for line in lines:
        fields = line.split()
        if len(fields) >= 4:
            point = MOUNT_ESCAPE.sub(lambda match: chr(int(match.group(1), 8)), fields[1])
            mounts.append((point, fields[2], fields[3]))
    return mounts


def mac_mounts():
    try:
        output = subprocess.run(["mount"], capture_output=True, text=True, check=False).stdout
    except OSError:
        return []
    mounts = []
    for line in output.splitlines():
        match = MAC_MOUNT_LINE.match(line)
        if match:
            mounts.append((match.group(1), match.group(2), match.group(3) or ""))
    return mounts


def mount_for(card, mounts):
    target = os.path.realpath(card)
    best = None
    for point, kind, options in mounts:
        inside = target == point or target.startswith(point.rstrip("/") + "/")
        if inside and (best is None or len(point) > len(best[0])):
            best = (point, kind, options)
    return best


def mount_option_offset(options):
    for option in options.split(","):
        if option == "tz=UTC":
            return 0
        if option.startswith("time_offset="):
            return int(option.split("=", 1)[1]) * 60
    return 0


def card_volume(name, options="", platform=sys.platform):
    kind = (name or "").lower()
    exfat = kind in EXFAT_NAMES
    if kind not in FAT_NAMES and not exfat:
        return CardVolume(name or "unknown", False, False, current_offset(), OTHER_CAVEAT)
    label = "exFAT" if exfat else "FAT32" if kind == "fat32" else "FAT"
    if exfat:
        return CardVolume(label, True, True, 0, "")
    if platform.startswith("linux"):
        return CardVolume(label, True, False, mount_option_offset(options), LINUX_CAVEAT)
    if platform == "win32":
        return CardVolume(label, True, False, current_offset(), "")
    return CardVolume(label, True, False, current_offset(), MAC_CAVEAT)


def detect_volume(card):
    name, options = file_system_name(card)
    return card_volume(name, options)


def card_root(text):
    if DRIVE_LETTER.match(text):
        text += os.sep
    return Path(text).expanduser().resolve()


def windows_drive_roots():
    import ctypes
    kernel32 = ctypes.windll.kernel32
    kernel32.SetErrorMode(SEM_FAILCRITICALERRORS)
    mask = kernel32.GetLogicalDrives()
    return [Path(f"{letter}:\\") for index, letter in enumerate(string.ascii_uppercase)
            if mask >> index & 1]


def mounted_roots(platform=sys.platform):
    if platform == "win32":
        return windows_drive_roots()
    if platform == "darwin":
        return sorted(Path("/Volumes").glob("*"))
    user = os.environ.get("USER", "")
    return sorted([*Path("/media", user).glob("*"), *Path("/run/media", user).glob("*")])


def windows_volume_label(root):
    import ctypes
    label = ctypes.create_unicode_buffer(64)
    found = ctypes.windll.kernel32.GetVolumeInformationW(
        ctypes.c_wchar_p(str(root)), label, len(label), None, None, None, None, 0)
    return label.value if found else None


def volume_label(root, platform=sys.platform):
    if platform == "win32":
        return windows_volume_label(root)
    return root.name


def player_internal_drives(card, roots):
    found = []
    for root in roots:
        try:
            if (volume_label(root) or "").strip().upper() != INTERNAL_LABEL:
                continue
            if root.resolve() != card:
                found.append((root, (root / RIPPL_FOLDER).is_dir()))
        except OSError:
            continue
    plain = [root for root, has_rippl in found if not has_rippl]
    return plain or [root for root, _ in found]


def player_cards(roots):
    cards = []
    for root in roots:
        try:
            if (root / RIPPL_FOLDER).is_dir():
                cards.append(root)
        except OSError:
            continue
    return cards


def typed_path(text, platform=sys.platform):
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "\"'":
        return text[1:-1]
    if platform != "win32":
        return text.replace("\\ ", " ")
    return text


def ask_for_card(cards, read=input):
    if len(cards) == 1:
        answer = typed_path(read(f"Card to sync [{cards[0]}]: "))
        return answer or str(cards[0])
    if cards:
        print("Cards that have been in a Rippl player: " +
              ", ".join(str(card) for card in cards))
    answer = typed_path(read("Card drive or folder (for example E: or /Volumes/CARD; you can "
                             "drag it here): "))
    if not answer:
        raise SyncError("No card given. Nothing was written.")
    return answer


def check_target(card, volume, rippl_exists):
    if not card.is_dir():
        raise SyncError(f"{card} isn't a folder or drive. Give the card's drive, for example E:\\")
    if rippl_exists:
        return
    if not os.path.ismount(card):
        raise SyncError(f"{card} isn't the top of a memory card. The player reads music from the "
                        "whole card, so give the card's drive (for example E:\\ on Windows or "
                        "/Volumes/CARD on a Mac).")
    if not volume.supported:
        raise SyncError(f"{card} is {volume.file_system}, not a FAT32 or exFAT memory card.")


def ensure_rippl_folder(rippl, options, assume_yes):
    if rippl.is_dir():
        return
    if options.dry_run:
        print(f"Would create {rippl}")
        return
    if not assume_yes:
        if not sys.stdin.isatty():
            raise SyncError(f"{rippl} doesn't exist yet. Run again with --yes to create it.")
        answer = input(f"{rippl} doesn't exist yet (the player hasn't used this card). "
                       "Create it? [y/N] ")
        if answer.strip().lower() not in ("y", "yes"):
            raise SyncError("Nothing was written.")
    rippl.mkdir()


def require_dependencies():
    packages = {"mutagen": "mutagen", "PIL": "Pillow"}
    missing = [package for module, package in packages.items()
               if importlib.util.find_spec(module) is None]
    if missing:
        raise SyncError(f"{', '.join(missing)} missing. Install the tools' requirements with: "
                        f'"{sys.executable}" -m pip install -r "{REQUIREMENTS}"')


def step(text):
    print(f"{text:<28}", end="", flush=True)


def human_size(size):
    if size >= MEBIBYTE:
        return f"{size / MEBIBYTE:.1f} MB"
    return f"{max(1, round(size / 1024))} KB"


def plural(count, word, words=None):
    return f"{count:,} {word if count == 1 else words or word + 's'}"


def internal_root(internal):
    return None if internal is None else internal.root


def card_path(prefix, relative):
    parts = library_index.music_path(prefix, relative).split("/")
    return b"/".join(card_name(part) for part in parts)


def read_music(card, volume, internal=None, excluded=frozenset()):
    step("Reading the music...")
    roots = library_index.MusicRoots(card, internal_root(internal), excluded)
    tracks, files, failures = [], [], []
    for base, path, prefix in roots.files(library_index.AUDIO_SUFFIXES):
        relative = path.relative_to(base)
        drive = volume if internal is None or base != internal.root else internal.volume
        try:
            stat = path.stat()
        except OSError as error:
            failures.append(f"{library_index.music_path(prefix, relative)}: {error}")
            continue
        try:
            track = library_index.read_track(base, path, prefix)
            tracks.append(track)
        except Exception as error:
            failures.append(f"{library_index.music_path(prefix, relative)}: {error}")
            track = None
        files.append((card_path(prefix, relative), stat.st_size & WORD_MASK,
                      card_stamp(stat, drive), track))
    playlists = [library_index.read_playlist(base, path, prefix)
                 for base, path, prefix in roots.files(library_index.PLAYLIST_SUFFIXES)]
    model = library_index.build_library(tracks, playlists)
    print(f"{plural(len(model.tracks), 'track')}, {plural(len(model.albums), 'album')}, "
          f"{plural(len(model.playlists), 'playlist')}")
    for failure in failures:
        print(f"  skipped (tags unreadable) {failure}")
    for playlist in model.playlists:
        if playlist.unresolved:
            missing = plural(playlist.unresolved, "entry", "entries")
            print(f"  playlist {playlist.name}: {missing} not found")
    return model, roots, files


def art_describer(volume, internal=None):
    if not volume.supported:
        return None

    def describe(path):
        drive = volume
        if internal is not None and Path(path).is_relative_to(internal.root):
            drive = internal.volume
        return card_name(path.name), card_stamp(path.stat(), drive)
    return describe


def build_covers(model, roots, old, volume, options, internal=None):
    step("Finding album covers..." if options.covers else "Keeping the album covers...")
    result = tide_covers.build(model, roots, old, art_describer(volume, internal),
                               decode=options.covers)
    print(f"{plural(result.covers, 'cover')} ({result.reused:,} kept), "
          f"{len(result.missing):,} without art")
    for album in result.missing if options.verbose else ():
        print(f"  no cover: {album}")
    return result


def stage_library(card_store, model, files):
    previous = card_store.value(VALUE_RECORDS)
    previous = previous if previous in (STREAM_RECORDS, STREAM_OTHER_RECORDS) else STREAM_RECORDS
    following = STREAM_OTHER_RECORDS if previous == STREAM_RECORDS else STREAM_RECORDS
    library = rippl_format.encode_library(model)
    card_store.write(STREAM_LIBRARY, store.POOL_LIBRARY, library)
    card_store.write(following, store.POOL_LIBRARY, records.encode_records(files))
    card_store.write(previous, store.POOL_LIBRARY, b"")
    card_store.set_region(REGION_LIBRARY, STREAM_LIBRARY, 0, len(library))
    card_store.set_value(VALUE_RECORDS, following)
    return library


def take_fingerprint(card, volume, internal=None, excluded=frozenset()):
    try:
        return card_fingerprint(card, volume, internal, excluded)
    except OSError as error:
        print(f"  couldn't read {error.filename}: {error.strerror}")
        return Fingerprint(False, 0, 0, 0)


def write_or_report(path, data, card_file, options):
    existing = existing_size(path)
    size = target_size(existing, len(data), card_file.standard_size)
    if options.dry_run:
        print(f"  would write {card_file.name}")
        return
    try:
        write_card_file(path, data, card_file)
    except OSError as error:
        raise SyncError(f"couldn't write {path}: {error.strerror or error}") from error
    print(f"  {card_file.name:<11} {human_size(size)}")


def commit_store(card_store, library, covers, options):
    try:
        added = card_store.new_bytes()
        if options.dry_run:
            print(f"  would write the library ({human_size(len(library))}) and "
                  f"{plural(covers.slots, 'cover')} into {store.MANIFEST_NAME}, adding "
                  f"{human_size(added)} of segment files")
            return
        card_store.commit()
    except (OSError, store.StoreError) as error:
        raise SyncError(f"couldn't write the library: {error}") from error
    print(f"  library     {human_size(len(library))}, {plural(covers.slots, 'cover')}"
          f" ({human_size(added)} of new segment files)")


def record_fingerprint(rippl, card, volume, before, options, internal=None,
                       excluded=frozenset()):
    step("Checking the music again...")
    after = take_fingerprint(card, volume, internal, excluded)
    if not (before.complete and after.complete):
        print("incomplete")
        print("  The music couldn't be fingerprinted completely (folders more than 31 levels "
              "deep, paths over 1,023 bytes or an unreadable file), so the player will rescan.")
        return False
    if before != after:
        print("changed")
        print("  The music changed while this ran. The player will rescan it.")
        return False
    print(f"{after.entries:,} files and folders, unchanged")
    path = rippl / SCAN_FILE.name
    record = scan_record(after, stored_version(read_prefix(path, SCAN_RECORD.size)))
    write_or_report(path, record, SCAN_FILE, options)
    return True


def sync(card, volume, options, internal=None, internal_missing=False, excluded=frozenset()):
    rippl = card / RIPPL_FOLDER
    before = None
    if options.fingerprint and not internal_missing:
        step("Fingerprinting the music...")
        before = take_fingerprint(card, volume, internal, excluded)
        print(f"{before.entries:,} files and folders" if before.complete else "incomplete")
    try:
        card_store = store.Store(rippl, options.dry_run)
        old = tide_covers.OldCovers(card_store)
    except store.StoreError as error:
        raise SyncError(f"the card's library can't be read: {error}") from error
    model, roots, files = read_music(card, volume, internal, excluded)
    covers = build_covers(model, roots, old, volume, options, internal)
    if (before is not None or internal_missing) and not options.dry_run:
        forget_fingerprint(rippl / SCAN_FILE.name)
    print(f"{'Planned for' if options.dry_run else 'Writing'} {rippl}")
    library = stage_library(card_store, model, files)
    tide_covers.stage(card_store, old, covers)
    commit_store(card_store, library, covers, options)
    recorded = before is not None and record_fingerprint(rippl, card, volume, before, options,
                                                         internal, excluded)
    if options.clock:
        write_clock(rippl, options)
    return recorded


def print_next_steps(options, recorded, volume, seconds):
    print(f"Done in {seconds:.0f} s.")
    if options.dry_run:
        print("Nothing was written (--dry-run).")
        return
    print("Next: eject the card safely, then put it back in the player.")
    if options.clock:
        print("The player sets its clock from this computer's when the USB session ends.")
    if recorded:
        print("The player will use this library without scanning the card again.")
        if volume.caveat:
            print(f"(Note: {volume.caveat}.)")
    elif options.fingerprint:
        print("The player will scan the card once more when it starts or after USB.")
    else:
        print("After USB the player compares the music with its own last scan, as usual.")
    if not options.covers:
        print("Covers were kept as they were: new albums show without a cover until the next "
              "rescan on the player (Settings, Rescan library).")


def run_check(card, volume, internal=None, excluded=frozenset()):
    print(f"Checking {card} ({volume.file_system})")
    if internal is not None:
        print(f"  with the internal storage {internal.root} ({internal.volume.file_system})")
    computed = take_fingerprint(card, volume, internal, excluded)
    stored = stored_fingerprint(read_prefix(card / RIPPL_FOLDER / SCAN_FILE.name,
                                            SCAN_RECORD.size))
    print(f"  this computer: {describe(computed)}")
    print(f"  the player's record: {describe(stored)}")
    if stored is not None and computed.complete and computed == stored:
        print("They match: the player won't rescan this card.")
        return 0
    if stored is None:
        print("The card has no fingerprint yet, so the player will rescan it after USB.")
        return 1
    print("They differ: either the music changed since the player's last scan, or this computer "
          "reads the file times differently from the player (see docs/user-guide.md).")
    return 1


def describe(fingerprint):
    if fingerprint is None:
        return "none"
    if not fingerprint.complete:
        return "incomplete"
    return (f"{fingerprint.first:08x} {fingerprint.second:08x}, "
            f"{fingerprint.entries:,} files and folders")


def options_from(arguments):
    return SyncOptions(covers=not arguments.no_covers,
                       fingerprint=not arguments.no_fingerprint, clock=not arguments.no_clock,
                       dry_run=arguments.dry_run, verbose=arguments.verbose)


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        prog="tide sync",
        description="Build the Rippl library and album covers for a memory card on this "
                    "computer, so the player doesn't have to scan it.")
    parser.add_argument("card", nargs="?",
                        help="the card's drive or mount point, for example E:\\ or "
                             "/Volumes/CARD; without it, the tool asks")
    parser.add_argument("--internal",
                        help=f"the player's internal storage drive, when it isn't found by its "
                             f"name ({INTERNAL_LABEL})")
    parser.add_argument("--no-covers", action="store_true",
                        help="build the library and keep the covers the card already has, "
                             "without finding new ones")
    parser.add_argument("--no-fingerprint", action="store_true",
                        help="leave scan.bin alone, so the player decides itself whether to "
                             "rescan")
    parser.add_argument("--no-clock", action="store_true",
                        help="don't write clock.bin, so the player keeps its own time")
    parser.add_argument("--check", action="store_true",
                        help="only compare this computer's music fingerprint with the player's")
    parser.add_argument("--dry-run", action="store_true",
                        help="do everything except writing to the card")
    parser.add_argument("--yes", action="store_true",
                        help="create the card's .rippl folder without asking")
    parser.add_argument("--verbose", action="store_true", help="list the albums without art")
    return parser.parse_args(argv)


def card_argument(arguments):
    if arguments.card is not None:
        return arguments.card
    if not sys.stdin.isatty():
        raise SyncError("Give the card's drive or mount point, for example E: or /Volumes/CARD.")
    return ask_for_card(player_cards(mounted_roots()))


def given_internal(text, card):
    root = card_root(text)
    if not root.is_dir():
        raise SyncError(f"{root} isn't a folder or drive. Give the player's internal storage "
                        "drive, for example F:\\")
    if root == card:
        raise SyncError("--internal names the card itself. Give the player's other drive.")
    return root


def internal_drive(arguments, card):
    if arguments.internal is not None:
        root = given_internal(arguments.internal, card)
    else:
        found = player_internal_drives(card, mounted_roots())
        if len(found) > 1:
            raise SyncError(f"More than one drive is called {INTERNAL_LABEL} "
                            f"({', '.join(str(root) for root in found)}). Give the player's "
                            "internal storage with --internal DRIVE.")
        if not found:
            print(f"The player's internal storage (the drive called {INTERNAL_LABEL}) wasn't "
                  "found, so the library is built from the card only and the player will scan "
                  "once more. Sync with the card in the connected player, or give the drive "
                  "with --internal DRIVE.")
            return None
        root = found[0]
    return InternalDrive(root, detect_volume(root))


def wait_before_closing():
    try:
        input("Press Enter to close.")
    except (EOFError, KeyboardInterrupt):
        pass


def main(argv=None):
    arguments = parse_arguments(argv)
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    try:
        return run(arguments)
    except (EOFError, KeyboardInterrupt):
        print("\nStopped. Nothing more was written.", file=sys.stderr)
        return 1
    finally:
        if arguments.card is None and sys.stdin.isatty():
            wait_before_closing()


def run(arguments):
    started = time.monotonic()
    try:
        card = card_root(card_argument(arguments))
        volume = detect_volume(card)
        rippl = card / RIPPL_FOLDER
        check_target(card, volume, rippl.is_dir())
        internal = internal_drive(arguments, card)
        excluded = library_index.read_excluded_folders(rippl / library_index.FOLDERS_NAME)
        if excluded:
            print(f"Leaving out {plural(len(excluded), 'top-level folder')} (Settings, Music "
                  "folders on the player).")
        if arguments.check:
            return run_check(card, volume, internal, excluded)
        require_dependencies()
        options = options_from(arguments)
        print(f"Tide sync: {card} ({volume.file_system})")
        if internal is not None:
            print(f"  and the internal storage {internal.root} ({internal.volume.file_system})")
        ensure_rippl_folder(rippl, options, arguments.yes)
        recorded = sync(card, volume, options, internal, internal is None, excluded)
    except SyncError as error:
        print(f"tide sync: {error}", file=sys.stderr)
        return 2
    print_next_steps(options, recorded, volume, time.monotonic() - started)
    return 0


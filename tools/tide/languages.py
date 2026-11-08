import argparse
import json
import shutil
import struct
import sys
from pathlib import Path

from tide import sync

FOLDER = "Languages"
MAGIC = b"ELNG"
VERSION = 1
HEADER = struct.Struct("<4sHHI")
PACK_LIMIT = 0x4000


class LanguageError(Exception):
    pass


def source_root():
    frozen = getattr(sys, "_MEIPASS", None)
    if frozen:
        return Path(frozen)
    return Path(__file__).resolve().parents[2]


def languages(root=None):
    root = source_root() if root is None else root
    path = root / "assets" / "lang" / "languages.json"
    return json.loads(path.read_text(encoding="utf-8"))["languages"]


def pack_folder(root=None):
    root = source_root() if root is None else root
    return root / "src" / "lang" / "packs"


def optional_languages(root=None):
    return [language for language in languages(root) if not language.get("builtin")]


def pack_problem(data):
    if len(data) < HEADER.size:
        return "shorter than its header"
    if len(data) > PACK_LIMIT:
        return f"{len(data):,} bytes, over the player's {PACK_LIMIT:,}"
    magic, version, count, size = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION:
        return "not a Rippl language pack of this version"
    if size > len(data) or size < HEADER.size + 2 * count:
        return "its size field doesn't match the file"
    return None


def internal_drive(text=None):
    if text:
        drive = Path(sync.typed_path(text))
        if not drive.is_dir():
            raise LanguageError(f"{drive} isn't a folder.")
        return drive
    drives = sync.player_internal_drives(None, sync.mounted_roots())
    if not drives:
        raise LanguageError(f"No drive named {sync.INTERNAL_LABEL} found. Connect the player "
                            "(Storage mode) or give the drive with --drive.")
    if len(drives) > 1:
        raise LanguageError("More than one drive named " + sync.INTERNAL_LABEL + ": " +
                            ", ".join(str(drive) for drive in drives) + ". Pick one with --drive.")
    return drives[0]


def installed(drive, known):
    folder = drive / FOLDER
    return [language for language in known if (folder / f"{language['code']}.bin").is_file()]


def install(drive, codes, root=None, dry_run=False):
    known = {language["code"]: language for language in optional_languages(root)}
    unknown = [code for code in codes if code not in known]
    if unknown:
        raise LanguageError("Unknown or built-in language: " + ", ".join(unknown) +
                            ". Choose from " + ", ".join(known) + ".")
    folder = drive / FOLDER
    for code in codes:
        source = pack_folder(root) / f"{code}.bin"
        data = source.read_bytes()
        problem = pack_problem(data)
        if problem:
            raise LanguageError(f"{source.name}: {problem}")
        print(f"  {known[code]['name']:<20} {FOLDER}/{code}.bin, {len(data):,} bytes")
        if not dry_run:
            folder.mkdir(exist_ok=True)
            shutil.copyfile(source, folder / f"{code}.bin")


def remove(drive, codes, dry_run=False):
    for code in codes:
        path = drive / FOLDER / f"{code}.bin"
        if path.is_file():
            print(f"  removing {FOLDER}/{code}.bin")
            if not dry_run:
                path.unlink()
        else:
            print(f"  {FOLDER}/{code}.bin isn't there")


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        prog="tide languages",
        description="Copy extra language packs to the player's internal drive (RIPPL). "
                    "English and Simplified Chinese are built in.")
    parser.add_argument("action", nargs="?", choices=("list", "install", "remove"),
                        default="list")
    parser.add_argument("codes", nargs="*", help="language codes, for example de ru")
    parser.add_argument("--drive", help="the RIPPL drive, if Tide doesn't find it")
    parser.add_argument("--dry-run", action="store_true", help="show what would change")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)
    try:
        drive = internal_drive(arguments.drive)
        known = optional_languages()
        if arguments.action == "install":
            if not arguments.codes:
                raise LanguageError("Name the languages to install, for example: "
                                    "tide languages install de")
            install(drive, arguments.codes, dry_run=arguments.dry_run)
        elif arguments.action == "remove":
            remove(drive, arguments.codes, dry_run=arguments.dry_run)
        present = {language["code"] for language in installed(drive, known)}
        print(f"Language packs on {drive}:")
        for language in known:
            mark = "installed" if language["code"] in present else ""
            print(f"  {language['code']}  {language['name']:<20} {mark}")
        print("Restart the player and choose the language in Settings → System → Language.")
    except (LanguageError, OSError) as error:
        print(f"tide languages: {error}", file=sys.stderr)
        return 2
    return 0

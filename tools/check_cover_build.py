import argparse
import base64
import contextlib
import io
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path
from unittest import mock

from mutagen import id3
from mutagen.flac import FLAC, Picture
from mutagen.mp4 import MP4, MP4Cover
from mutagen.oggopus import OggOpus

import check_cover_decode
import cover_export
from tide import sync as tide_sync

RECORD_BYTES = check_cover_decode.RECORD_BYTES
TOOL_COUNTS = re.compile(r"(\d+) covers, (\d+) reused, \d+ missing, (\d+) of (\d+) files kept")
NANOSECONDS = 1_000_000_000


def image(width, height, seed):
    return check_cover_decode.smooth(width, height, seed)


def encoded(picture, kind):
    buffer = io.BytesIO()
    picture.save(buffer, kind, quality=90) if kind == "JPEG" else picture.save(buffer, kind)
    return buffer.getvalue()


def flac_track(path, source, album, number, picture=None):
    shutil.copyfile(source / "tagged.flac", path)
    audio = FLAC(path)
    audio.delete()
    audio.clear_pictures()
    audio["album"], audio["albumartist"], audio["artist"] = album, "Tester", "Tester"
    audio["title"], audio["tracknumber"] = f"{album} {number}", str(number)
    if picture is not None:
        block = Picture()
        block.type, block.mime, block.data = 3, "image/jpeg", picture
        audio.add_picture(block)
    audio.save()


def mp3_track(path, source, album, pictures):
    shutil.copyfile(source / "id3v24.mp3", path)
    tags = id3.ID3()
    for frame in (id3.TALB(text=album), id3.TPE2(text="Tester"), id3.TPE1(text="Tester"),
                  id3.TIT2(text=f"{album} 1"), id3.TRCK(text="1")):
        tags.add(frame)
    for index, (kind, mime, data) in enumerate(pictures):
        tags.add(id3.APIC(encoding=0, mime=mime, type=kind, desc=str(index), data=data))
    tags.save(path)


def mp4_track(path, source, album, picture):
    shutil.copyfile(source / "aac.m4a", path)
    audio = MP4(path)
    audio.delete()
    audio["\xa9alb"], audio["aART"], audio["\xa9ART"] = [album], ["Tester"], ["Tester"]
    audio["\xa9nam"], audio["trkn"] = [f"{album} 1"], [(1, 0)]
    audio["covr"] = [MP4Cover(picture, imageformat=MP4Cover.FORMAT_JPEG)]
    audio.save()


def opus_track(path, source, album, number, pictures=()):
    shutil.copyfile(source.parent / "opus" / "tagged.opus", path)
    audio = OggOpus(path)
    audio.tags.clear()
    audio["album"], audio["albumartist"], audio["artist"] = album, "Tester", "Tester"
    audio["title"], audio["tracknumber"] = f"{album} {number}", str(number)
    blocks = []
    for kind, data in pictures:
        block = Picture()
        block.type, block.mime, block.data = kind, "image/jpeg", data
        blocks.append(base64.b64encode(block.write()).decode("ascii"))
    if blocks:
        audio["metadata_block_picture"] = blocks
    audio.save()


def build_flat_folder(root, source, expected):
    first, second, shared = image(360, 360, 37), image(280, 280, 38), image(200, 200, 39)
    (root / "Flat").mkdir()
    flac_track(root / "Flat" / "a1.flac", source, "Flat A", 1)
    flac_track(root / "Flat" / "a2.flac", source, "Flat A", 2, encoded(first, "JPEG"))
    expected["Flat A"] = encoded(first, "JPEG")
    opus_track(root / "Flat" / "b1.opus", source, "Flat B", 1)
    opus_track(root / "Flat" / "b2.opus", source, "Flat B", 2)
    opus_track(root / "Flat" / "b3.opus", source, "Flat B", 3, [(3, encoded(second, "JPEG"))])
    expected["Flat B"] = encoded(second, "JPEG")
    flac_track(root / "Flat" / "c1.flac", source, "Flat C", 1)
    (root / "Flat" / "cover.jpg").write_bytes(encoded(shared, "JPEG"))
    expected["Flat C"] = encoded(shared, "JPEG")
    for number in range(1, 6):
        flac_track(root / "Flat" / f"d{number}.flac", source, "Flat D", number,
                   encoded(first, "JPEG") if number == 5 else None)
    expected["Flat D"] = encoded(shared, "JPEG")


def build_music(root, source):
    expected = {}
    front, other, folder_jpeg = image(600, 600, 31), image(300, 200, 32), image(400, 400, 33)
    late_front, disc_two, mp4 = image(500, 500, 34), image(260, 260, 35), image(320, 320, 36)
    (root / "Front").mkdir(parents=True)
    flac_track(root / "Front" / "01.flac", source, "Front", 1, encoded(front, "JPEG"))
    flac_track(root / "Front" / "02.flac", source, "Front", 2, encoded(other, "JPEG"))
    expected["Front"] = encoded(front, "JPEG")
    (root / "Other").mkdir()
    mp3_track(root / "Other" / "01.mp3", source, "Other", [(0, "image/png", encoded(other, "PNG"))])
    expected["Other"] = encoded(other, "PNG")
    (root / "Late").mkdir()
    mp3_track(root / "Late" / "01.mp3", source, "Late",
              [(0, "image/png", encoded(other, "PNG")), (3, "image/jpeg", encoded(late_front, "JPEG"))])
    expected["Late"] = encoded(late_front, "JPEG")
    (root / "Folder").mkdir()
    flac_track(root / "Folder" / "01.flac", source, "Folder", 1)
    (root / "Folder" / "folder.png").write_bytes(encoded(other, "PNG"))
    (root / "Folder" / "Cover.JPG").write_bytes(encoded(folder_jpeg, "JPEG"))
    expected["Folder"] = encoded(folder_jpeg, "JPEG")
    (root / "Split" / "CD1").mkdir(parents=True)
    (root / "Split" / "CD2").mkdir()
    flac_track(root / "Split" / "CD1" / "01.flac", source, "Split", 1)
    flac_track(root / "Split" / "CD2" / "02.flac", source, "Split", 2)
    (root / "Split" / "CD2" / "folder.jpg").write_bytes(encoded(disc_two, "JPEG"))
    expected["Split"] = encoded(disc_two, "JPEG")
    (root / "Mp4").mkdir()
    mp4_track(root / "Mp4" / "01.m4a", source, "Mp4", encoded(mp4, "JPEG"))
    expected["Mp4"] = encoded(mp4, "JPEG")
    (root / "None").mkdir()
    flac_track(root / "None" / "01.flac", source, "None", 1)
    opus_front = image(640, 640, 40)
    (root / "Opus").mkdir()
    opus_track(root / "Opus" / "01.opus", source, "Opus", 1,
               [(0, encoded(other, "JPEG")), (3, encoded(opus_front, "JPEG"))])
    expected["Opus"] = encoded(opus_front, "JPEG")
    build_flat_folder(root, source, expected)
    return expected


def read_dump(data):
    covers = {}
    offset = 0
    while offset < len(data):
        (length,) = struct.unpack_from("<H", data, offset)
        title = data[offset + 2:offset + 2 + length].decode("utf-8")
        offset += 2 + length
        present = data[offset]
        offset += 1
        if present:
            covers[title] = data[offset:offset + RECORD_BYTES]
            offset += RECORD_BYTES
    return covers


def check(covers, expected):
    failures = []
    if set(covers) != set(expected):
        failures.append(f"covers for {sorted(covers)}, expected {sorted(expected)}")
    for album, data in expected.items():
        record = covers.get(album)
        if record is None:
            failures.append(f"{album}: no cover")
            continue
        reference = check_cover_decode.reference_record(cover_export.load_image(data))
        mean, worst, p99 = check_cover_decode.compare(record, reference)
        print(f"{album}: mean {mean:.2f}, 99% {p99}, worst {worst}")
        if mean > 4.0 or p99 > 48:
            failures.append(f"{album}: mean {mean:.2f}, 99% {p99}")
    return failures


def run_tool(tool, music, output):
    result = subprocess.run([str(tool), str(music), str(output)], capture_output=True, text=True)
    print(result.stdout.strip())
    if result.returncode != 0:
        print(f"FAIL tool exit {result.returncode}: {result.stderr}", file=sys.stderr)
    return result


def reuse_failures(tool, music, output):
    result = run_tool(tool, music, output)
    counts = TOOL_COUNTS.search(result.stdout)
    if result.returncode != 0 or counts is None:
        return ["the second build failed"]
    covers, reused = int(counts.group(1)), int(counts.group(2))
    if covers != reused:
        return [f"the second build kept {reused} of {covers} covers"]
    return []


def simulator_stamp(stat, volume):
    return stat.st_mtime_ns // NANOSECONDS & 0xFFFFFFFF


def sync_like_the_simulator(card):
    volume = tide_sync.CardVolume("simulator", True, False, 0, "")
    with mock.patch.object(tide_sync, "card_stamp", simulator_stamp), \
            mock.patch.object(tide_sync, "detect_volume", lambda root: volume), \
            contextlib.redirect_stdout(io.StringIO()) as output:
        status = tide_sync.main([str(card), "--yes", "--no-clock", "--no-fingerprint"])
    if status != 0:
        print(output.getvalue(), file=sys.stderr)
    return status


def tide_failures(tool, music, output, work):
    card = work / "tide"
    shutil.copytree(music, card)
    (card / ".rippl").mkdir()
    if sync_like_the_simulator(card) != 0:
        return ["tide sync failed"]
    compared = subprocess.run([str(tool), "--compare", str(output), str(card)],
                              capture_output=True, text=True)
    print(f"tide: {compared.stdout.strip()}")
    failures = [] if compared.returncode == 0 else ["Tide's library differs from the player's"]
    result = run_tool(tool, card, card)
    counts = TOOL_COUNTS.search(result.stdout)
    if result.returncode != 0 or counts is None:
        return failures + ["the player couldn't rebuild a card Tide synced"]
    covers, reused, kept, files = (int(value) for value in counts.groups())
    if covers != reused or kept != files:
        failures.append(f"after Tide the player kept {reused} of {covers} covers and "
                        f"{kept} of {files} files")
    return failures


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="Check covers built on the device side")
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    shutil.rmtree(arguments.work, ignore_errors=True)
    music = arguments.work / "music"
    output = arguments.work / "output"
    output.mkdir(parents=True)
    expected = build_music(music, arguments.fixtures / "tags")
    if run_tool(arguments.tool, music, output).returncode != 0:
        return 1
    failures = check(read_dump((output / "covers.dump").read_bytes()), expected)
    failures += reuse_failures(arguments.tool, music, output)
    failures += tide_failures(arguments.tool, music, output, arguments.work)
    for failure in failures:
        print(f"FAIL {failure}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

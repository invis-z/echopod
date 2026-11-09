import argparse
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

FIXTURES = [
    {"name": "lpc_16_44k_stereo", "rate": 44100, "bits": 16, "channels": 2, "frames": 6000,
     "flags": ["-8"], "signal": "music"},
    {"name": "lpc_24_96k_stereo", "rate": 96000, "bits": 24, "channels": 2, "frames": 5000,
     "flags": ["-8"], "signal": "music"},
    {"name": "fixed_16_mono", "rate": 44100, "bits": 16, "channels": 1, "frames": 5000,
     "flags": ["-0"], "signal": "music"},
    {"name": "constant_and_noise", "rate": 44100, "bits": 16, "channels": 2, "frames": 9000,
     "flags": ["-5", "--blocksize=1024"], "signal": "gaps"},
    {"name": "wasted_bits_24", "rate": 48000, "bits": 24, "channels": 2, "frames": 4500,
     "flags": ["-8"], "signal": "wasted"},
    {"name": "odd_blocks_16", "rate": 44100, "bits": 16, "channels": 2, "frames": 3001,
     "flags": ["-6", "--blocksize=1152"], "signal": "music"},
    {"name": "independent_16", "rate": 22050, "bits": 16, "channels": 2, "frames": 2500,
     "flags": ["-8", "--no-mid-side"], "signal": "music"},
    {"name": "big_blocks_16", "rate": 44100, "bits": 16, "channels": 2, "frames": 9000,
     "flags": ["-5", "--lax", "--blocksize=8192"], "signal": "music"},
    {"name": "stereo_32_independent", "rate": 48000, "bits": 32, "channels": 2, "frames": 6000,
     "flags": ["-ch_mode", "indep"], "signal": "opposed", "encoder": "ffmpeg"},
    {"name": "stereo_32_left_side", "rate": 48000, "bits": 32, "channels": 2, "frames": 6000,
     "flags": ["-ch_mode", "left_side"], "signal": "opposed", "encoder": "ffmpeg"},
    {"name": "stereo_32_side_right", "rate": 48000, "bits": 32, "channels": 2, "frames": 6000,
     "flags": ["-ch_mode", "right_side"], "signal": "opposed", "encoder": "ffmpeg"},
    {"name": "stereo_32_mid_side", "rate": 48000, "bits": 32, "channels": 2, "frames": 6000,
     "flags": ["-ch_mode", "mid_side"], "signal": "opposed", "encoder": "ffmpeg"},
    {"name": "stereo_32_mid_side_fixed", "rate": 44100, "bits": 32, "channels": 2,
     "frames": 6000, "flags": ["-ch_mode", "mid_side", "-compression_level", "0"],
     "signal": "opposed", "encoder": "ffmpeg"},
    {"name": "mono_32", "rate": 96000, "bits": 32, "channels": 1, "frames": 5000,
     "flags": [], "signal": "music", "encoder": "ffmpeg"},
]


class Noise:
    def __init__(self, seed):
        self.state = seed

    def next(self):
        self.state = (self.state * 1103515245 + 12345) & 0x7FFFFFFF
        return self.state / 0x7FFFFFFF * 2 - 1


def music_sample(index, channel, rate, peak, noise):
    time = index / rate
    tone = math.sin(2 * math.pi * (440 + 220 * channel) * time)
    overtone = 0.3 * math.sin(2 * math.pi * 1250 * time + channel)
    return int((0.55 * tone + 0.25 * overtone + 0.05 * noise.next()) * peak)


def gap_sample(index, channel, peak, noise):
    segment = index // 1500
    if segment % 3 == 0:
        return 0
    if segment % 3 == 1:
        return int(noise.next() * peak * 0.9)
    return -1234 if channel == 0 else 4321


def samples_for(fixture):
    peak = (1 << (fixture["bits"] - 1)) - 1
    noise = Noise(1 + fixture["channels"] + fixture["bits"])
    rows = []
    for index in range(fixture["frames"]):
        row = []
        for channel in range(fixture["channels"]):
            if fixture["signal"] == "gaps":
                value = gap_sample(index, channel, peak, noise)
            elif fixture["signal"] == "opposed":
                value = music_sample(index, 0, fixture["rate"], peak, noise)
                value = value if channel == 0 else -value + int(noise.next() * peak * 0.05)
            elif fixture["signal"] == "wasted":
                value = music_sample(index, channel, fixture["rate"], 32767, noise) << 8
            else:
                value = music_sample(index, channel, fixture["rate"], peak, noise)
            row.append(max(-peak - 1, min(peak, value)))
        rows.append(row)
    return rows


def raw_bytes(rows, bits):
    width = bits // 8
    out = bytearray()
    for row in rows:
        for value in row:
            out += (value & ((1 << bits) - 1)).to_bytes(width, "little")
    return bytes(out)


def expected_bytes(rows):
    return b"".join(struct.pack("<i", value) for row in rows for value in row)


def encode_ffmpeg(fixture, raw, output):
    sample_format = {16: "s16le", 24: "s24le", 32: "s32le"}[fixture["bits"]]
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", sample_format,
                    "-ar", str(fixture["rate"]), "-ac", str(fixture["channels"]), "-i", "pipe:",
                    "-c:a", "flac", "-sample_fmt", "s32" if fixture["bits"] > 16 else "s16",
                    "-strict", "experimental", *fixture["flags"], str(output)],
                   input=raw, check=True)
    decoded = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(output),
                              "-f", sample_format, "pipe:"], capture_output=True, check=True)
    if decoded.stdout != raw:
        raise ValueError(f"{output} does not decode back to its samples")


def encode(fixture, raw, output):
    if fixture.get("encoder") == "ffmpeg":
        encode_ffmpeg(fixture, raw, output)
        return
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "input.raw"
        source.write_bytes(raw)
        subprocess.run(["flac", "--silent", "--force", "--force-raw-format", "--endian=little",
                        "--sign=signed", f"--channels={fixture['channels']}",
                        f"--bps={fixture['bits']}", f"--sample-rate={fixture['rate']}",
                        "--no-seektable", *fixture["flags"], "-o", str(output), str(source)],
                       check=True)


def parse_arguments():
    parser = argparse.ArgumentParser(description="Write the FLAC files and expected samples used by the C tests")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--only", nargs="*", help="write just these fixtures")
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    arguments.output.mkdir(parents=True, exist_ok=True)
    for fixture in FIXTURES:
        if arguments.only and fixture["name"] not in arguments.only:
            continue
        rows = samples_for(fixture)
        encode(fixture, raw_bytes(rows, fixture["bits"]), arguments.output / f"{fixture['name']}.flac")
        (arguments.output / f"{fixture['name']}.pcm").write_bytes(expected_bytes(rows))
        print(f"wrote {fixture['name']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

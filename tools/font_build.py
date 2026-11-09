import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

ALPHA_LEVELS = 15
FULL_ALPHA = 255
DEFAULT_LEVEL_BITS = 4
BYTE_BITS = 8
BYTES_PER_LINE = 16
MAX_BITMAP_BYTES = 0xFFFF


@dataclass
class GlyphImage:
    codepoint: int
    width: int
    height: int
    left: int
    top: int
    advance: int
    levels: list


@dataclass
class FontData:
    name: str
    ascent: int
    descent: int
    glyphs: list
    pack_faces: tuple = (0, 0)
    level_bits: int = DEFAULT_LEVEL_BITS


def quantize_alpha(value, level_bits=DEFAULT_LEVEL_BITS):
    top = (1 << level_bits) - 1
    return (value * top + FULL_ALPHA // 2) // FULL_ALPHA


def bitmap_bytes(width, height, level_bits=DEFAULT_LEVEL_BITS):
    per_byte = BYTE_BITS // level_bits
    return (width * height + per_byte - 1) // per_byte


def pack_levels(levels, level_bits=DEFAULT_LEVEL_BITS):
    per_byte = BYTE_BITS // level_bits
    padded = list(levels) + [0] * (-len(levels) % per_byte)
    packed = bytearray()
    for start in range(0, len(padded), per_byte):
        value = 0
        for level in padded[start:start + per_byte]:
            value = (value << level_bits) | level
        packed.append(value)
    return bytes(packed)


def pack_nibbles(levels):
    return pack_levels(levels)


def load_font(path, size, layout_engine=None):
    return ImageFont.truetype(str(path), size, layout_engine=layout_engine)


def covered_codepoints(path, ranges):
    cmap = TTFont(str(path))["cmap"].getBestCmap()
    wanted = [codepoint for first, last in ranges for codepoint in range(first, last + 1)]
    return sorted(codepoint for codepoint in set(wanted) if codepoint in cmap)


def empty_glyph(codepoint, advance):
    return GlyphImage(codepoint, 0, 0, 0, 0, advance, [])


def render_glyph(font, codepoint, level_bits=DEFAULT_LEVEL_BITS):
    character = chr(codepoint)
    advance = round(font.getlength(character))
    left, upper, right, lower = font.getbbox(character, anchor="ls")
    if right <= left or lower <= upper:
        return empty_glyph(codepoint, advance)
    image = Image.new("L", (right - left, lower - upper), 0)
    ImageDraw.Draw(image).text((-left, -upper), character, font=font, fill=FULL_ALPHA, anchor="ls")
    quantized = image.point(lambda value: quantize_alpha(value, level_bits))
    ink = quantized.getbbox()
    if ink is None:
        return empty_glyph(codepoint, advance)
    cropped = quantized.crop(ink)
    return GlyphImage(codepoint, cropped.width, cropped.height, left + ink[0], -(upper + ink[1]),
                      advance, list(cropped.getdata()))


def pack_face_numbers(assets_directory):
    path = assets_directory / "glyph_pack.json"
    if not path.exists():
        return {}
    faces = json.loads(path.read_text())["faces"]
    return {face["name"]: number for number, face in enumerate(faces, start=1)}


def build_font(spec, ranges, assets_directory):
    path = assets_directory / spec["file"]
    font = load_font(path, spec["size"])
    ascent, descent = font.getmetrics()
    ranges = spec.get("ranges", ranges)
    glyphs = [render_glyph(font, codepoint) for codepoint in covered_codepoints(path, ranges)]
    numbers = pack_face_numbers(assets_directory)
    faces = tuple(numbers[name] if name else 0 for name in spec.get("pack_faces", [None, None]))
    return FontData(spec["name"], ascent, descent, glyphs, faces,
                    spec.get("level_bits", DEFAULT_LEVEL_BITS))


def constant_name(name):
    return "FONT_" + name.upper()


def byte_lines(data):
    for start in range(0, len(data), BYTES_PER_LINE):
        chunk = data[start:start + BYTES_PER_LINE]
        yield "    " + ", ".join(f"0x{value:02X}" for value in chunk) + ","


def glyph_table(glyphs):
    lines = []
    bitmap = bytearray()
    for glyph in sorted(glyphs, key=lambda item: item.codepoint):
        lines.append(f"    {{{glyph.codepoint}, {len(bitmap)}, {glyph.width}, {glyph.height}, "
                     f"{glyph.left}, {glyph.top}, {glyph.advance}}},")
        bitmap += pack_nibbles(glyph.levels)
    if len(bitmap) > MAX_BITMAP_BYTES:
        raise ValueError(f"bitmap of {len(bitmap)} bytes does not fit 16-bit offsets")
    return lines, bytes(bitmap)


def font_tables(font):
    prefix = font.name.upper()
    if not font.glyphs:
        return [], []
    glyph_lines, bitmap = glyph_table(font.glyphs)
    tables = [
        f"RIPPL_RESIDENT static const uint8_t {prefix}_BITMAP[] = {{",
        *(list(byte_lines(bitmap)) or ["    0x00,"]),
        "};",
        "",
        f"RIPPL_RESIDENT static const Glyph {prefix}_GLYPHS[] = {{",
        *glyph_lines,
        "};",
        "",
    ]
    fields = [
        f"    .glyphs = {prefix}_GLYPHS,",
        f"    .bitmap = {prefix}_BITMAP,",
        f"    .glyph_count = {len(font.glyphs)},",
        f"    .bitmap_size = {len(bitmap)},",
    ]
    return tables, fields


def font_source(font):
    tables, fields = font_tables(font)
    return "\n".join([
        *tables,
        f"const Font {constant_name(font.name)} = {{",
        *fields,
        f"    .ascent = {font.ascent},",
        f"    .descent = {font.descent},",
        *([f"    .pack_faces = {{{', '.join(str(face) for face in font.pack_faces)}}},"]
          if any(font.pack_faces) else []),
        *([f"    .level_bits = {font.level_bits},"]
          if font.level_bits != DEFAULT_LEVEL_BITS else []),
        "};",
        "",
    ])


def header_source(names):
    declarations = [f"extern const Font {constant_name(name)};" for name in names]
    return "\n".join(["#pragma once", "", '#include "gfx/font.h"', "", *declarations, ""])


def fonts_source(fonts):
    parts = ['#include "fonts/fonts.h"', "", "#include <stdint.h>", "",
             '#include "core/resident.h"', ""]
    parts += [font_source(font) for font in fonts]
    return "\n".join(parts)


def formatted(source, style_file):
    formatter = shutil.which("clang-format")
    if not formatter:
        return source
    result = subprocess.run([formatter, f"--style=file:{style_file}", "--assume-filename=fonts.c"],
                            input=source, check=True, capture_output=True, text=True)
    return result.stdout


def generated_outputs(config_path, style_file):
    config = json.loads(config_path.read_text())
    fonts = [build_font(spec, config["character_ranges"], config_path.parent)
             for spec in config["fonts"]]
    outputs = {
        "include/fonts/fonts.h": formatted(header_source([font.name for font in fonts]), style_file),
        "fonts.c": formatted(fonts_source(fonts), style_file),
    }
    return fonts, outputs


def stale_outputs(generated, committed):
    return sorted(name for name, content in generated.items() if committed.get(name) != content)


def committed_outputs(output_directory, names):
    return {name: (output_directory / name).read_text()
            for name in names if (output_directory / name).exists()}


def write_outputs(output_directory, outputs):
    for name, content in outputs.items():
        path = output_directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)


def parse_arguments():
    parser = argparse.ArgumentParser(description="Render TTF fonts into 4-bit C glyph tables")
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--style", type=Path, required=True)
    parser.add_argument("--check", action="store_true",
                        help="fail if the committed output differs from a fresh build")
    return parser.parse_args()


def report(fonts):
    for font in fonts:
        _, bitmap = glyph_table(font.glyphs)
        print(f"{font.name}: {len(font.glyphs)} glyphs, {len(bitmap):,} bitmap bytes")


def main():
    arguments = parse_arguments()
    fonts, outputs = generated_outputs(arguments.config, arguments.style)
    if arguments.check:
        stale = stale_outputs(outputs, committed_outputs(arguments.output, outputs))
        for name in stale:
            print(f"out of date: {arguments.output / name}")
        return 1 if stale else 0
    write_outputs(arguments.output, outputs)
    report(fonts)
    return 0


if __name__ == "__main__":
    sys.exit(main())

import argparse
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

VIEW_BOX = 160
SUPERSAMPLE = 16
CURVE_STEPS = 24
LEVELS = 15
SIZES = {"RIPPL_LOGO_SPLASH": 80}
TOKEN = re.compile(r"[A-Za-z]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")
ARGUMENTS = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6, "S": 4, "Q": 4, "T": 2, "Z": 0}


def cubic(start, first, second, end):
    points = []
    for step in range(1, CURVE_STEPS + 1):
        t = step / CURVE_STEPS
        u = 1 - t
        points.append((u * u * u * start[0] + 3 * u * u * t * first[0] + 3 * u * t * t * second[0]
                       + t * t * t * end[0],
                       u * u * u * start[1] + 3 * u * u * t * first[1] + 3 * u * t * t * second[1]
                       + t * t * t * end[1]))
    return points


def quadratic(start, control, end):
    first = (start[0] + 2 / 3 * (control[0] - start[0]), start[1] + 2 / 3 * (control[1] - start[1]))
    second = (end[0] + 2 / 3 * (control[0] - end[0]), end[1] + 2 / 3 * (control[1] - end[1]))
    return cubic(start, first, second, end)


def commands(path):
    tokens = TOKEN.findall(path)
    index = 0
    command = None
    while index < len(tokens):
        if tokens[index].isalpha():
            command = tokens[index]
            index += 1
            if command.upper() not in ARGUMENTS:
                raise ValueError(f"the path command {command} isn't supported")
        elif command is None:
            raise ValueError("a path must start with a command")
        count = ARGUMENTS[command.upper()]
        values = [float(token) for token in tokens[index:index + count]]
        if len(values) != count:
            raise ValueError(f"{command} needs {count} numbers")
        index += count
        yield command, values
        if command in "Mm":
            command = "l" if command == "m" else "L"
        if count == 0:
            command = None


def subpaths(path):
    shapes = []
    points = []
    position = (0.0, 0.0)
    start = position
    control = None
    previous = None
    for command, values in commands(path):
        relative = command.islower()
        kind = command.upper()
        base = position if relative else (0.0, 0.0)

        def at(x, y):
            return (base[0] + x, base[1] + y)

        if kind == "M":
            if len(points) > 1:
                shapes.append(points)
            position = at(*values)
            start = position
            points = [position]
        elif kind == "L":
            position = at(*values)
            points.append(position)
        elif kind == "H":
            position = (values[0] + (position[0] if relative else 0.0), position[1])
            points.append(position)
        elif kind == "V":
            position = (position[0], values[0] + (position[1] if relative else 0.0))
            points.append(position)
        elif kind == "C":
            first, second, end = at(*values[0:2]), at(*values[2:4]), at(*values[4:6])
            points += cubic(position, first, second, end)
            control, position = second, end
        elif kind == "S":
            first = (2 * position[0] - control[0], 2 * position[1] - control[1]) \
                if previous in "CS" else position
            second, end = at(*values[0:2]), at(*values[2:4])
            points += cubic(position, first, second, end)
            control, position = second, end
        elif kind == "Q":
            control, end = at(*values[0:2]), at(*values[2:4])
            points += quadratic(position, control, end)
            position = end
        elif kind == "T":
            control = (2 * position[0] - control[0], 2 * position[1] - control[1]) \
                if previous in "QT" else position
            end = at(*values)
            points += quadratic(position, control, end)
            position = end
        else:
            if len(points) > 1:
                shapes.append(points)
            points = [start]
            position = start
        previous = kind
    if len(points) > 1:
        shapes.append(points)
    return shapes


def coverage(svg, side):
    paths = re.findall(r'\sd="([^"]+)"', svg)
    if not paths:
        raise ValueError("the logo has no paths")
    big = side * SUPERSAMPLE
    scale = big / VIEW_BOX
    filled = Image.new("L", (big, big), 0)
    for path in paths:
        shape = Image.new("L", (big, big), 0)
        for points in subpaths(path):
            piece = Image.new("L", (big, big), 0)
            ImageDraw.Draw(piece).polygon([(x * scale, y * scale) for x, y in points], fill=255)
            shape = ImageChops.logical_xor(shape.convert("1"), piece.convert("1")).convert("L")
        filled = ImageChops.lighter(filled, shape)
    small = filled.resize((side, side), Image.BOX)
    return [round(value * LEVELS / 255) for value in small.get_flattened_data()]


def packed(levels):
    padded = levels + [0] * (len(levels) % 2)
    return [padded[index] | padded[index + 1] << 4 for index in range(0, len(padded), 2)]


def render(svg, style):
    source = ['#include "rippl_logo_data.h"', ""]
    header = ["#pragma once", "", "#include <stdint.h>", "", "enum {"]
    header += [f"    {name}_SIDE = {side}," for name, side in SIZES.items()]
    header += [f"    RIPPL_LOGO_LEVELS = {LEVELS}", "};", ""]
    for name, side in SIZES.items():
        data = packed(coverage(svg, side))
        header.append(f"extern const uint8_t {name}_COVERAGE[{len(data)}];")
        source.append(f"const uint8_t {name}_COVERAGE[{len(data)}] = {{")
        source.append(", ".join(f"0x{value:02X}" for value in data) + "};")
        source.append("")
    header.append("")
    return {"rippl_logo_data.c": formatted("\n".join(source), style),
            "rippl_logo_data.h": formatted("\n".join(header), style)}


def formatted(text, style):
    result = subprocess.run(["clang-format", f"--style=file:{style}", "--assume-filename=x.c"],
                            input=text, capture_output=True, text=True, check=True)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description="Make the Rippl logo's coverage masks")
    parser.add_argument("--svg", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--style", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    files = render(arguments.svg.read_text(), arguments.style)
    stale = [name for name, text in files.items()
             if not (arguments.output / name).exists() or
             (arguments.output / name).read_text() != text]
    if arguments.check:
        for name in stale:
            print(f"out of date: {arguments.output / name}")
        return 1 if stale else 0
    for name, text in files.items():
        (arguments.output / name).write_text(text)
    print(f"wrote {', '.join(f'{side} px' for side in SIZES.values())} logo masks to "
          f"{arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

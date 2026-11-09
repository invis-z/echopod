import argparse
import math
import subprocess
import sys
from pathlib import Path

FACTORS = (2, 3, 4, 6)
TAPS = 64
PASS_EDGE = 0.41
STOP_EDGE = 0.5
STOPBAND_DB = 90.0
COEFFICIENT_BITS = 30
SERIES_TERMS = 40


def bessel_i0(x):
    total, term = 1.0, 1.0
    for k in range(1, SERIES_TERMS):
        term *= (x / (2 * k)) ** 2
        total += term
    return total


def kaiser_beta(attenuation_db):
    return 0.1102 * (attenuation_db - 8.7)


def prototype(factor):
    length = TAPS * factor
    cutoff = (PASS_EDGE + STOP_EDGE) / 2 / factor
    beta = kaiser_beta(STOPBAND_DB)
    middle = (length - 1) / 2
    scale = bessel_i0(beta)
    taps = []
    for index in range(length):
        offset = index - middle
        argument = 2 * cutoff * offset
        sinc = 1.0 if offset == 0 else math.sin(math.pi * argument) / (math.pi * argument)
        window = bessel_i0(beta * math.sqrt(1 - (offset / middle) ** 2)) / scale
        taps.append(2 * cutoff * sinc * window)
    return taps


def phases(factor):
    taps = prototype(factor)
    one = 1 << COEFFICIENT_BITS
    rows = []
    for phase in range(factor):
        row = [taps[phase + tap * factor] for tap in range(TAPS)]
        total = sum(row)
        fixed = [round(value / total * one) for value in row]
        fixed[TAPS // 2] += one - sum(fixed)
        rows.append(fixed)
    return rows


def render(style):
    lines = ['#include "upsampler_table.h"', ""]
    for factor in FACTORS:
        lines.append(f"static const int32_t PHASES_{factor}[{factor}][UPSAMPLER_TAPS] = {{")
        for row in phases(factor):
            lines.append("    {" + ", ".join(str(value) for value in row) + "},")
        lines += ["};", ""]
    lines.append("const UpsamplerFilter UPSAMPLER_FILTERS[UPSAMPLER_FILTER_COUNT] = {")
    for factor in FACTORS:
        lines.append(f"    {{{factor}, &PHASES_{factor}[0][0]}},")
    lines += ["};", ""]
    header = ["#pragma once", "", "#include <stdint.h>", "", "enum {",
              f"    UPSAMPLER_TAPS = {TAPS},", f"    UPSAMPLER_COEFFICIENT_BITS = {COEFFICIENT_BITS},",
              f"    UPSAMPLER_FILTER_COUNT = {len(FACTORS)}", "};", "",
              "typedef struct UpsamplerFilter {", "    uint32_t factor;",
              "    const int32_t *coefficients;", "} UpsamplerFilter;", "",
              "extern const UpsamplerFilter UPSAMPLER_FILTERS[UPSAMPLER_FILTER_COUNT];", ""]
    return {"upsampler_table.c": formatted("\n".join(lines), style),
            "upsampler_table.h": formatted("\n".join(header), style)}


def formatted(text, style):
    result = subprocess.run(["clang-format", f"--style=file:{style}", "--assume-filename=x.c"],
                            input=text, capture_output=True, text=True, check=True)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description="Generate the upsampler's polyphase filters")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--style", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    stale = []
    for name, text in render(arguments.style).items():
        path = arguments.output / name
        if arguments.check:
            if not path.exists() or path.read_text() != text:
                stale.append(path)
        else:
            path.write_text(text)
    for path in stale:
        print(f"out of date: {path}")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())

import argparse
import re
import subprocess
import sys
from pathlib import Path

CODE2_START = 0x03076670
CODE2_END = 0x0307A000
SCANNED_SECTIONS = (".code", ".resident")
FUNCTION = re.compile(r"^([0-9a-f]{8}) <([^>]+)>:$")


def read_allowed(path):
    calls, addresses = set(), set()
    for number, line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip():
            continue
        parts = line.split(maxsplit=2)
        if len(parts) < 3 or parts[1] not in ("calls", "addresses"):
            raise SystemExit(f"{path}:{number}: expected NAME calls|addresses REASON")
        (calls if parts[1] == "calls" else addresses).add(parts[0])
    return calls, addresses


def base_name(symbol):
    return re.sub(r"\.(?:lto_priv|isra|constprop|part|cold)\.\d+", "", symbol)


def in_code2(address):
    return CODE2_START <= (address & ~1) < CODE2_END


def symbols(nm, elf):
    output = subprocess.run([nm, "-n", "-S", "--defined-only", str(elf)], check=True,
                            capture_output=True, text=True).stdout
    table = []
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-2] in "tTdDrRbB":
            table.append((int(parts[0], 16), parts[-1]))
    return table


def owner(table, address):
    found = None
    for start, name in table:
        if start > address:
            break
        found = name
    return found or "?"


def sections(objdump, elf):
    output = subprocess.run([objdump, "-h", str(elf)], check=True, capture_output=True,
                            text=True).stdout
    found = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 6 and parts[1] in SCANNED_SECTIONS:
            found[parts[1]] = (int(parts[3], 16), int(parts[2], 16), int(parts[5], 16))
    return found


def branch_problems(objdump, elf, allowed):
    output = subprocess.run([objdump, "-d", "--no-show-raw-insn", "-j", ".code", str(elf)],
                            check=True, capture_output=True, text=True).stdout
    problems = []
    current = None
    halves = {}
    for line in output.splitlines():
        match = FUNCTION.match(line)
        if match:
            current = match.group(2)
            continue
        parts = line.split("\t")
        if len(parts) < 3 or current is None:
            continue
        mnemonic = parts[1].strip()
        immediate = re.match(r"(r\d+|ip|lr), #(\d+)", parts[2].strip())
        if mnemonic.startswith("movw") and immediate:
            halves[immediate.group(1)] = int(immediate.group(2))
        elif mnemonic.startswith("movt") and immediate:
            value = int(immediate.group(2)) << 16 | halves.get(immediate.group(1), 0)
            if in_code2(value) and base_name(current) not in allowed:
                problems.append(f"{current} builds the address {value:#010x} in code 2")
        if not re.fullmatch(r"(?:bl|blx|b|cbn?z|b[a-z]{2})(?:\.[wn])?", mnemonic):
            continue
        target = re.search(r"\b([0-9a-f]{7,8})\b", parts[2])
        if target and in_code2(int(target.group(1), 16)) and base_name(current) not in allowed:
            problems.append(f"{current} branches into code 2 ({parts[2].strip()})")
    return problems


def pointer_problems(objdump, nm, elf, allowed):
    data = Path(elf).read_bytes()
    table = symbols(nm, elf)
    problems = []
    for name, (address, size, offset) in sections(objdump, elf).items():
        for at in range(0, size - 3, 4):
            word = int.from_bytes(data[offset + at:offset + at + 4], "little")
            if word & 1 and in_code2(word):
                holder = owner(table, address + at)
                if base_name(holder) not in allowed:
                    target = owner(table, word & ~1)
                    problems.append(f"{holder} ({name}) holds the address of {target}")
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description="Check that nothing reaches code 2 unguarded")
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--allowed", type=Path, required=True)
    parser.add_argument("--objdump", default="arm-none-eabi-objdump")
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    arguments = parser.parse_args(argv)
    calls, addresses = read_allowed(arguments.allowed)
    problems = branch_problems(arguments.objdump, arguments.elf, calls)
    problems += pointer_problems(arguments.objdump, arguments.nm, arguments.elf,
                                 calls | addresses)
    for problem in sorted(set(problems)):
        print(f"code 2: {problem}", file=sys.stderr)
    if problems:
        print("Guard the caller with hal_system_code2_ready() and list it in "
              f"{arguments.allowed.name}, or keep the callee out of code 2.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

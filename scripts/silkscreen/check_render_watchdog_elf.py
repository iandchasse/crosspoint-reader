#!/usr/bin/env python3
"""Checks a built firmware image for the Silkscreen render watchdog (docs/silkscreen/POWER.md, AW-04).

test_render_watchdog.py reads the SOURCE: it cannot see whether `#if FREEINK_CAP_POWER_POLICY` was true where each hook
sits. A merge that drops an include can make the macro read as 0 in one file; every env still builds and every source
check still passes, and the watchdog is silently off. This reads the machine code of the linked image instead.

  python -I -X utf8 scripts/silkscreen/check_render_watchdog_elf.py [env | --elf PATH] [--absent] [--control]

Without --absent (the Silkscreen envs): the image must call the watchdog's hooks from the functions they belong to,
and its task must exist:
  ActivityManager::renderTaskLoop()  calls renderwatchdog::beginRender(...) and renderwatchdog::endRender()
  ActivityManager::begin(...)        calls renderwatchdog::start()
  loop()                             calls renderwatchdog::noteLoopPass()
  setup()                            calls renderwatchdog::takeSleepRequest(...), renderwatchdog::takeReport(...) and
                                     sleepWithoutDrivers()
With --absent (every other board's env): the image must hold no renderwatchdog:: symbol and no sleepWithoutDrivers().

--control, on an image that passes, asks each function about a hook that belongs to another one (loop() about
beginRender, setup() about start, ...): every such question must come back "not called", which shows that the check
can say no.

The binutils come from $NM / $OBJDUMP, else PlatformIO's toolchain-xtensa-esp-elf (the RISC-V boards' images, for
--absent, from toolchain-riscv32-esp). A call is a direct call
(callN <symbol>) or a call through a literal (l32r into a register, then callxN), which is how a call across the
image's 5 MB of flash is made. Exit 0 when everything holds, 1 when something does not, 2 when the check cannot run.
"""
import argparse
import bisect
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGES = Path.home() / ".platformio" / "packages"
# ELF machine -> (binutils directory, prefix): the Silkscreen is an ESP32-S3; the other boards may be RISC-V (ESP32-C3)
TOOLCHAINS = {
    94: (PACKAGES / "toolchain-xtensa-esp-elf" / "bin", "xtensa-esp32s3-elf-"),
    243: (PACKAGES / "toolchain-riscv32-esp" / "bin", "riscv32-esp-elf-"),
}

# function-name prefixes (demangled; a clone such as `setup() [clone .part.0]` counts; `|` separates the functions that
# together hold the calls: the compiler may inline main.cpp's static helpers into setup() or leave them out of line)
# -> calls they must contain
EXPECTED = {
    "ActivityManager::renderTaskLoop()": ["renderwatchdog::beginRender(", "renderwatchdog::endRender()"],
    "ActivityManager::begin(": ["renderwatchdog::start()"],
    "loop()": ["renderwatchdog::noteLoopPass()"],
    "setup()|sleepIfRenderWatchdog()|sleepIfResetLoop()": [
        "renderwatchdog::takeSleepRequest(",
        "renderwatchdog::takeReport(",
        "sleepWithoutDrivers()",
    ],
}


MACHINE = 94


def tool(name: str, env: str) -> str:
    given = os.environ.get(env)
    if given:
        return given
    directory, prefix = TOOLCHAINS[MACHINE]
    for suffix in ("", ".exe"):
        candidate = directory / f"{prefix}{name}{suffix}"
        if candidate.exists():
            return str(candidate)
    sys.exit(f"check_render_watchdog_elf: no {prefix}{name} in {directory} (set ${env} to the binutils')")


def run(cmd: list[str]) -> str:
    done = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=900)
    if done.returncode != 0:
        sys.exit(f"check_render_watchdog_elf: {' '.join(cmd[:2])} failed: {done.stderr.strip()[:200]}")
    return done.stdout


class Image:
    """The loadable sections of an ELF32, for reading the literal a call is made through."""

    def __init__(self, path: Path):
        self.data = path.read_bytes()
        if self.data[:4] != b"\x7fELF":
            sys.exit(f"check_render_watchdog_elf: {path} is not an ELF file")
        shoff = struct.unpack_from("<I", self.data, 32)[0]
        shentsize, shnum = struct.unpack_from("<HH", self.data, 46)
        self.sections = []
        for i in range(shnum):
            _, kind, flags, addr, off, size = struct.unpack_from("<IIIIII", self.data, shoff + i * shentsize)
            if kind == 1 and flags & 2 and size:  # PROGBITS, allocated
                self.sections.append((addr, off, size))

    def word(self, addr: int):
        for base, off, size in self.sections:
            if base <= addr < base + size:
                return struct.unpack_from("<I", self.data, off + addr - base)[0]
        return None


def symbols(elf: Path):
    """(address, size, demangled name) of every function symbol, sorted by address."""
    out = run([tool("nm", "NM"), "-S", "-C", "--defined-only", str(elf)])
    found = []
    for line in out.splitlines():
        parts = line.split(None, 3)
        if len(parts) == 4 and parts[2] in "tTwW":
            try:
                found.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
            except ValueError:
                pass
    found.sort()
    return found


def calls_in(elf: Path, image: Image, syms, start: int, size: int) -> set[str]:
    """Names of the functions called from the code at [start, start + size)."""
    addrs = [s[0] for s in syms]

    def name_at(address: int) -> str:
        k = bisect.bisect_right(addrs, address) - 1
        if k >= 0 and syms[k][0] <= address < syms[k][0] + max(syms[k][1], 1):
            return syms[k][2]
        return f"?{address:#x}"

    text = run([tool("objdump", "OBJDUMP"), "-d", "-C", f"--start-address={start:#x}", f"--stop-address={start + size:#x}",
                str(elf)])
    literals: dict[str, int | None] = {}
    found: set[str] = set()
    for line in text.splitlines():
        m = re.match(r"^\s*[0-9a-f]+:\t[0-9a-f ]+\t(\S+)\s*(.*)$", line)
        if not m:
            continue
        op, args = m.group(1), m.group(2)
        if op.startswith("l32r"):
            a = re.match(r"(a\d+), ([0-9a-f]+)", args)
            if a:
                literals[a.group(1)] = image.word(int(a.group(2), 16))
        elif op.startswith("callx"):
            reg = args.split()[0] if args.split() else ""
            target = literals.get(reg)
            if target is not None:
                found.add(name_at(target))
        elif op.startswith("call"):
            t = re.match(r"([0-9a-f]+) <", args)
            if t:
                found.add(name_at(int(t.group(1), 16)))
        elif re.match(r"(movi|mov|addi|add|l32i)", op):
            literals.pop(args.split(",")[0].strip(), None)
    return found


def check_present(elf: Path, expected=None) -> list[str]:
    image = Image(elf)
    syms = symbols(elf)
    expected = EXPECTED if expected is None else expected
    problems = []
    if not any(name.startswith("renderwatchdog::start()") for _, _, name in syms):
        problems.append("the image has no renderwatchdog::start(): RenderWatchdog.cpp is empty here")
    for prefix, wanted in expected.items():
        prefixes = prefix.split("|")
        homes = [s for s in syms if s[2].startswith(tuple(prefixes))]
        if not any(s[2].startswith(prefixes[0]) for s in homes):
            problems.append(f"no function `{prefixes[0]}` in the image")
            continue
        seen: set[str] = set()
        for start, size, _ in homes:
            seen |= calls_in(elf, image, syms, start, size)
        for call in wanted:
            if not any(name.startswith(call) for name in seen):
                problems.append(f"`{prefix}` does not call `{call}`")
    return problems


def control(elf: Path) -> list[str]:
    """Each hook asked of a function that does not hold it: all must be reported missing."""
    hooks = [(f, c) for f, calls in EXPECTED.items() for c in calls]
    wrong = []
    for function, _ in hooks:
        for owner, call in hooks:
            if owner != function and not any(c == call for c in EXPECTED[function]):
                wrong.append((function, call))
    problems = []
    for function, call in wrong:
        if not check_present(elf, {function: [call]}):
            problems.append(f"CONTROL NOT CAUGHT: `{function}` is not expected to call `{call}`, and the check passed it")
    print(f"{len(wrong)} wrong pairings asked, {len(problems)} passed")
    return problems


def check_absent(elf: Path) -> list[str]:
    names = [name for _, _, name in symbols(elf) if name.startswith(("renderwatchdog::", "sleepWithoutDrivers"))]
    return [f"this image holds {len(names)} watchdog symbol(s), e.g. `{names[0]}`"] if names else []


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("env", nargs="?", default="silkscreen", help="PlatformIO env whose .pio/build/<env>/firmware.elf")
    parser.add_argument("--elf", help="an ELF file instead of the env's")
    parser.add_argument("--absent", action="store_true", help="the image must hold no watchdog (other boards)")
    parser.add_argument("--control", action="store_true", help="ask for hooks in the wrong functions: all must fail")
    args = parser.parse_args()
    elf = Path(args.elf) if args.elf else ROOT / ".pio" / "build" / args.env / "firmware.elf"
    if not elf.exists():
        print(f"check_render_watchdog_elf: no {elf}: build the env first")
        return 2
    label = args.env if not args.elf else elf.name
    global MACHINE
    MACHINE = struct.unpack_from("<H", elf.read_bytes()[:20], 18)[0]
    if MACHINE not in TOOLCHAINS:
        print(f"check_render_watchdog_elf: {label} is an ELF of machine {MACHINE}, which this does not read")
        return 2
    if MACHINE != 94 and not args.absent:
        print(f"{label}: a Silkscreen image is an Xtensa one; this is machine {MACHINE}")
        return 1
    problems = control(elf) if args.control else check_absent(elf) if args.absent else check_present(elf)
    for problem in problems:
        print(f"{label}: {problem}")
    if problems:
        return 1
    if args.control:
        print(f"{label}: every wrong pairing is reported missing, as it must be")
        return 0
    print(f"{label}: " + ("no render watchdog in the image, as it must be" if args.absent else
                          "the render watchdog's hooks are called where they belong (renderTaskLoop, begin, loop, setup)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Checks a built firmware image for the Silkscreen render watchdog (docs/silkscreen/POWER.md, AW-04).

test_render_watchdog.py reads the SOURCE: it cannot see whether `#if FREEINK_CAP_POWER_POLICY` was true where each hook
sits. A merge that drops an include can make the macro read as 0 in one file; every env still builds and every source
check still passes, and the watchdog is silently off. This reads the machine code of the linked image instead.

  python -I -X utf8 scripts/silkscreen/check_render_watchdog_elf.py [env | --elf PATH] [--absent] [--control]

Without --absent (the Silkscreen envs): the image must call the watchdog's hooks from the functions they belong to,
and its task must exist (EXPECTED below is the table):
  ActivityManager::renderTaskLoop()  calls renderwatchdog::beginRender(...) and renderwatchdog::endRender()
  ActivityManager::begin(...)        calls renderwatchdog::start()
  loop()                             calls renderwatchdog::noteLoopPass()
  enterDeepSleep(...)                calls renderwatchdog::beginSleepEntry()
  setup() and the static helpers     call takeSleepRequest, takeReport, resetLoopSlept, holdsLogs, describe,
  it may inline                      holdLastLogs, clearLastLogs and sleepWithoutDrivers()
  EpubReaderActivity::*, HomeActivity::*, TxtReaderActivity::*   call renderwatchdog::progress() (reader images;
                                     not asked of CrossPoint's test firmware, which has no reader)
With --absent (every other board's env): the image must hold no renderwatchdog:: symbol and no sleepWithoutDrivers(),
and must be a whole image (it has setup() and loop()), so that an image with its symbols stripped cannot pass.

--control, on an image that passes (it refuses any other), asks each function about a hook that belongs to another
one (loop() about beginRender, setup() about start, ...): every such question must come back "not called", which shows
that the check can say no. The questions are the distinct (function, hook) pairs of the table, and the count printed is
that number.

The binutils come from $NM / $OBJDUMP, else PlatformIO's toolchain-xtensa-esp-elf (the RISC-V boards' images, for
--absent, from toolchain-riscv32-esp). The whole image is disassembled once, in one pass, and every question is
answered from that. A call is a direct call (callN <symbol>) or a call through a literal (l32r into a register, then
callxN), which is how a call across the image's 5 MB of flash is made. Exit 0 when everything holds, 1 when something
does not, 2 when the check cannot run.
"""
import argparse
import bisect
import os
import re
import struct
import subprocess
import sys
import tempfile
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
    "enterDeepSleep(": ["renderwatchdog::beginSleepEntry()"],
    "setup()|sleepIfRenderWatchdog()|sleepIfResetLoop()|logWatchdogReport(": [
        "renderwatchdog::takeSleepRequest(",
        "renderwatchdog::takeReport(",
        "renderwatchdog::resetLoopSlept()",
        "renderwatchdog::holdsLogs()",
        "renderwatchdog::describe(",
        "holdLastLogs(",
        "clearLastLogs()",
        "sleepWithoutDrivers()",
    ],
    # the long renders that re-stamp (one call per chunk of a section build, per cover, per twenty pages of a text index)
    "EpubReaderActivity::": ["renderwatchdog::progress()"],
    "HomeActivity::": ["renderwatchdog::progress()"],
    "TxtReaderActivity::": ["renderwatchdog::progress()"],
}
# Entries whose class does not exist in every app (CrossInk has a text reader that CrossPoint does not): an image
# without any such function is not asked.
OPTIONAL = {"TxtReaderActivity::"}
# The long renders live in the reader image. CrossPoint's hardware test firmware (its symbols are in `testsuite::`) has
# no reader and no Home screen, so they are not asked of it; every reader image is asked, and a missing class is a failure.
READER_ENTRIES = {"EpubReaderActivity::", "HomeActivity::", "TxtReaderActivity::"}

MACHINE = 94
HEADER = re.compile(r"^([0-9a-f]+) <(.*)>:$")
INSN = re.compile(r"^\s*[0-9a-f]+:\t[0-9a-f ]+\t(\S+)\s*(.*)$")


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


class Analysis:
    """An image read once: its function symbols, and for each function the names it calls."""

    def __init__(self, elf: Path, disassemble: bool = True):
        self.syms = symbols(elf)
        self.test_firmware = any(name.startswith("testsuite::") for _, _, name in self.syms)
        self.calls: dict[int, set[str]] = {}
        if disassemble:
            self._disassemble(elf, Image(elf))

    def _disassemble(self, elf: Path, image: Image) -> None:
        addrs = [s[0] for s in self.syms]

        def name_at(address: int) -> str:
            k = bisect.bisect_right(addrs, address) - 1
            if k >= 0 and self.syms[k][0] <= address < self.syms[k][0] + max(self.syms[k][1], 1):
                return self.syms[k][2]
            return f"?{address:#x}"

        with tempfile.TemporaryFile() as errors:
            proc = subprocess.Popen([tool("objdump", "OBJDUMP"), "-d", "-C", str(elf)], stdout=subprocess.PIPE,
                                    stderr=errors, text=True, encoding="utf-8", errors="replace")
            current: set[str] | None = None
            literals: dict[str, int | None] = {}
            for line in proc.stdout:
                head = HEADER.match(line)
                if head:
                    current = self.calls.setdefault(int(head.group(1), 16), set())
                    literals = {}
                    continue
                if current is None:
                    continue
                m = INSN.match(line)
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
                        current.add(name_at(target))
                elif op.startswith("call"):
                    t = re.match(r"([0-9a-f]+) <", args)
                    if t:
                        current.add(name_at(int(t.group(1), 16)))
                elif re.match(r"(movi|mov|addi|add|l32i)", op):
                    literals.pop(args.split(",")[0].strip(), None)
            code = proc.wait()
            if code != 0:
                errors.seek(0)
                sys.exit(f"check_render_watchdog_elf: objdump failed: "
                         f"{errors.read().decode('utf-8', 'replace').strip()[:200]}")

    def homes(self, prefix: str):
        """The function symbols a table key names (`|` separates alternatives)."""
        return [s for s in self.syms if s[2].startswith(tuple(prefix.split("|")))]

    def has_home(self, prefix: str) -> bool:
        first = prefix.split("|")[0]
        return any(s[2].startswith(first) for s in self.homes(prefix))

    def called_by(self, prefix: str) -> set[str]:
        seen: set[str] = set()
        for start, _, _ in self.homes(prefix):
            seen |= self.calls.get(start, set())
        return seen


def check_present(analysis: Analysis, expected=None) -> list[str]:
    expected = EXPECTED if expected is None else expected
    problems = []
    if not any(name.startswith("renderwatchdog::start()") for _, _, name in analysis.syms):
        problems.append("the image has no renderwatchdog::start(): RenderWatchdog.cpp is empty here")
    for prefix, wanted in expected.items():
        if prefix in READER_ENTRIES and analysis.test_firmware:
            continue
        if not analysis.has_home(prefix):
            if prefix not in OPTIONAL:
                problems.append(f"no function `{prefix.split('|')[0]}` in the image")
            continue
        seen = analysis.called_by(prefix)
        for call in wanted:
            if not any(name.startswith(call) for name in seen):
                problems.append(f"`{prefix}` does not call `{call}`")
    return problems


def control(analysis: Analysis) -> list[str]:
    """Each hook asked of a function that does not hold it: all must be reported missing. Only on an image that passes:
    on one that does not, the questions would come back "missing" for the wrong reason."""
    present = check_present(analysis)
    if present:
        return [f"--control needs an image that passes the check; it reports: {present[0]}"]
    live = {function: calls for function, calls in EXPECTED.items() if analysis.has_home(function)
            and not (function in READER_ENTRIES and analysis.test_firmware)}
    wrong = sorted({(function, call) for function in live for owner in live if owner != function
                    for call in live[owner] if call not in live[function]})
    problems = []
    for function, call in wrong:
        if not check_present(analysis, {function: [call]}):
            problems.append(f"CONTROL NOT CAUGHT: `{function}` is not expected to call `{call}`, and the check passed it")
    print(f"{len(wrong)} distinct wrong pairings asked, {len(problems)} passed")
    return problems


def check_absent(analysis: Analysis) -> list[str]:
    # An image whose symbols are stripped holds no watchdog symbol either: it must be a whole image to count.
    names = [name for _, _, name in analysis.syms]
    if not any(n.startswith("setup()") for n in names) or not any(n.startswith("loop()") for n in names):
        return ["this image has no setup() or no loop() symbol (stripped?): its absence of watchdog symbols proves nothing"]
    found = [n for n in names if n.startswith(("renderwatchdog::", "sleepWithoutDrivers"))]
    return [f"this image holds {len(found)} watchdog symbol(s), e.g. `{found[0]}`"] if found else []


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
    # Looking for symbols by name needs no disassembly (the other boards' images, absent, are read by name only).
    analysis = Analysis(elf, disassemble=not args.absent)
    problems = control(analysis) if args.control else check_absent(analysis) if args.absent else check_present(analysis)
    for problem in problems:
        print(f"{label}: {problem}")
    if problems:
        return 1
    if args.control:
        print(f"{label}: every wrong pairing is reported missing, as it must be")
        return 0
    print(f"{label}: " + ("no render watchdog in the image, as it must be" if args.absent else
                          "the render watchdog's hooks are called where they belong (renderTaskLoop, begin, loop, "
                          "enterDeepSleep, setup, the long renders)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

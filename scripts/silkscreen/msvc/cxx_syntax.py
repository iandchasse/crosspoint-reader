"""A `c++` that only syntax-checks with the PlatformIO Xtensa g++ (-fsyntax-only), for the SDK host runners that
MSVC cannot link: the tests that `#define private public` mangle differently under cl.exe. The compile step
passes or fails as with a real g++; the run step then finds no binary, which is the limit of this mode.

XTENSA_GXX names another g++. The default is toolchain-xtensa-esp-elf's (14.2): the esp32s3 package's 8.4
fails 17 of the battery log core's static_asserts. See README.md in this directory.
"""
import os
import pathlib
import subprocess
import sys


def find_gxx():
    named = os.environ.get("XTENSA_GXX")
    if named:
        return named
    root = pathlib.Path.home() / ".platformio" / "packages" / "toolchain-xtensa-esp-elf" / "bin"
    for name in ("xtensa-esp-elf-g++.exe", "xtensa-esp-elf-g++"):
        if (root / name).is_file():
            return str(root / name)
    sys.exit("cxx_syntax: no xtensa g++ found (set XTENSA_GXX)")


def main(args):
    kept, i = [], 0
    while i < len(args):
        if args[i] == "-o":  # no binary is made
            i += 2
            continue
        if args[i] != "-fsyntax-only":
            kept.append(args[i])
        i += 1
    return subprocess.run([find_gxx(), "-fsyntax-only"] + kept).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

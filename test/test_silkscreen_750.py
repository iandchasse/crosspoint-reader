"""Compile the real firmware tag scanner: each panel family rejects the others."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="silkscreen-750-tag-") as directory:
    folder = Path(directory)
    (folder / "BoardConfig.h").write_text("#pragma once\n")
    harness = folder / "tag.cpp"
    harness.write_text(r'''
#include "FirmwareBoardTag.h"
#include <cassert>
#include <cstring>
#include <string>
int main() {
  assert(std::string(board_tag::boardName(), board_tag::boardNameLen()) == EXPECTED_NAME);
  for (const char* family : {"silkscreen", "silkscreen-368", "silkscreen-750"}) {
    const std::string image = std::string("noiseCROSSPOINT-BOARD-V1:") + family + ";tail";
    for (size_t step : {1, 3, 64}) {
      board_tag::Scanner scanner;
      for (size_t offset = 0; offset < image.size(); offset += step)
        scanner.feed(reinterpret_cast<const uint8_t*>(image.data() + offset),
                     std::min(step, image.size() - offset));
      assert(scanner.mismatch() == (std::strcmp(family, EXPECTED_NAME) != 0));
    }
  }
}
''')
    for name, variant in [("silkscreen", []), ("silkscreen-368", ["-DFREEINK_SILKSCREEN_368=1"]),
                          ("silkscreen-750", ["-DFREEINK_SILKSCREEN_750=1"])]:
        binary = folder / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-DFREEINK_DEVICE_SILKSCREEN=1",
                        f'-DEXPECTED_NAME="{name}"', *variant, "-I" + str(folder),
                        "-I" + str(ROOT / "src/network"), str(harness),
                        str(ROOT / "src/network/FirmwareBoardTag.cpp"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print("4.26/3.68/7.5 firmware tags reject cross-family SD/OTA updates in both directions")

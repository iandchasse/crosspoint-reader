"""Exercise production battery-maintenance blocks without a display or device."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def blocks(repo):
    source = (repo / "src/main.cpp").read_text(encoding="utf-8")
    estimator = re.search(
        r"#if FREEINK_DEVICE_SILKSCREEN && !CROSSPOINT_TEST_SUITE\n"
        r"(?:(?!#endif).)*?powerManager\.updateBatteryDisplay\(\)[^\n]*\n#endif",
        source, re.S,
    )
    assert estimator, repo
    marker = source.index("efresh the battery icon when USB is plugged or unplugged.")
    start = source.rfind("\n", 0, marker) + 1
    end_marker = ("  const unsigned long activityStartTime" if repo.name == "crosspoint-reader"
                  else "  // Background delivery must not hold up text entry")
    end = source.index(end_marker, start)
    return estimator.group() + "\n" + source[start:end]


HARNESS = r"""
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <cstdio>
namespace BoardConfig {
bool isSilkscreen() { return FREEINK_DEVICE_SILKSCREEN; }
}
unsigned long now;
unsigned long millis() { return now; }
struct Power {
  unsigned samples = 0;
  uint16_t actual = 35, cached = 35;
  bool updateBatteryDisplay() {
    ++samples;
    const bool changed = cached != actual;
    cached = actual;
    return changed;
  }
  uint16_t getBatteryPercentage() const { return cached; }
} powerManager;
struct Gpio {
  bool edge = false, usb = false;
  bool wasUsbStateChanged() const { return edge; }
  bool isUsbConnected() const { return usb; }
  bool isUsbConnectedCached() const { return usb; }
} gpio;
struct ActivityManager {
  unsigned requests = 0;
  bool reader = false;
  bool isReaderActivity() const { return reader; }
  void requestUpdate() { ++requests; }
} activityManager;
void maintain() {
__PRODUCTION__
}
int main() {
#if FREEINK_DEVICE_SILKSCREEN
  // Exercise both a reader page and another UI screen, with and without USB.
  for (bool reader : {false, true}) {
    activityManager.reader = reader;
    for (bool usb : {false, true}) {
      gpio.usb = usb;
      gpio.edge = true;
      for (uint16_t percent : {40, 55, 99, 100}) {
        powerManager.actual = percent;
        now += 60001;
        maintain();
        assert(activityManager.requests == 0);
      }
    }
  }
#if !CROSSPOINT_TEST_SUITE
  assert(powerManager.samples == 16);
  assert(powerManager.getBatteryPercentage() == 100);
#else
  assert(powerManager.samples == 0);
#endif
#else
  // Preserve other boards' existing cable-change repaint behavior.
  for (bool reader : {false, true}) {
    activityManager.requests = 0;
    activityManager.reader = reader;
    gpio.usb = false;
    gpio.edge = true;
    maintain();
    assert(activityManager.requests == (CROSSINK ? 1u : unsigned(!reader)));
  }
  assert(powerManager.samples == 0);
#if CROSSINK
  // CrossInk's legacy charging timer remains active for other boards.
  gpio.edge = false;
  gpio.usb = true;
  activityManager.requests = 0;
  now = 60001;
  maintain();
  powerManager.cached = 40;
  now += 60001;
  maintain();
  assert(activityManager.requests == 1);
#endif
#endif
  puts("PASS: estimates advance without battery-only Silkscreen redraws; other boards preserved");
}
"""

with tempfile.TemporaryDirectory(prefix="battery-redraw-") as directory:
    temp = Path(directory)
    for repo in [ROOT, ROOT.parent / "crossink"]:
        cpp = temp / (repo.name + ".cpp")
        cpp.write_text(HARNESS.replace("__PRODUCTION__", blocks(repo)), encoding="utf-8")
        for silk, suite in [(1, 0), (1, 1), (0, 0)]:
            binary = temp / f"{repo.name}-{silk}-{suite}.exe"
            subprocess.run([
                "c++", "-std=c++17",
                f"-DFREEINK_DEVICE_SILKSCREEN={silk}", f"-DCROSSPOINT_TEST_SUITE={suite}",
                f"-DCROSSINK={int(repo.name == 'crossink')}", str(cpp), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)

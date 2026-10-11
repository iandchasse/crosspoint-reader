#!/usr/bin/env bash
# No-touch audit of a built Silkscreen 3.68" image (docs/silkscreen/MODULARITY.md, "The 3.68" variant").
#
# The variant has no touch at all: no tail on J4, FREEINK_CAP_TOUCH=0, the board's profile with NO_TOUCH.
# Nothing may then probe 0x38, 0x5D or 0x14, reset TP_RST or send a hibernate at a sleep entry, and the
# build proves it by what is linked. This reads the image's symbol table and fails when:
#   - a touch driver body is linked (the FT5x06 / FT6336U / GT911 / CST816S / GSLX680 / Picco / CHSC6x
#     begin, poll and sleep code, the FocalTech id check, the hibernate and the multi-touch classifiers),
#     or one of InputManager's touch entry points that must be a stub is not;
#   - the SSD1677 or UC8279 X4 driver, or the generic display probe (the two-pass VER / FLG / MTP read
#     that would take a UC8253 for an UltraChip part), is linked: the variant's panel is the X3's and its
#     controller is read by the stock X3 protocol only;
#   - the X3 drivers (Uc8253X3Driver, Uc8279Driver) are not linked, so a pass would prove nothing;
#   - in a test image, the T11 / T12 touch tests are linked, or their no-touch stub is not;
#   - the image's board tag is not the variant's (silkscreen-368, silkscreen-368-test for the test image).
#
# Usage: scripts/silkscreen/check_no_touch.sh [env]    (default: silkscreen-368; build it first)
# Exits 0 on a pass, 1 when a check fails (each is listed), 2 when the image or nm cannot be read.
# The nm binary comes from $NM, else from PlatformIO's Xtensa toolchain. ELF=<file> and BIN=<file> name
# another image than .pio/build/<env>/firmware.{elf,bin}: the negative control is this script on the 4.26"
# board's silkscreen image, which must fail on the touch driver and the SSD1677 driver.
set -u
cd "$(dirname "$0")/../.." || exit 2

env="${1:-silkscreen-368}"
elf="${ELF:-.pio/build/${env}/firmware.elf}"
bin="${BIN:-.pio/build/${env}/firmware.bin}"
for f in "$elf" "$bin"; do
  if [ ! -f "$f" ]; then
    echo "missing $f: build the ${env} env first"
    exit 2
  fi
done

nm_bin="${NM:-}"
if [ -z "$nm_bin" ]; then
  for candidate in "$HOME"/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm \
    "$HOME"/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm.exe; do
    if [ -x "$candidate" ]; then
      nm_bin="$candidate"
      break
    fi
  done
fi
if [ -z "$nm_bin" ]; then
  echo "no nm found: set NM to the toolchain's nm"
  exit 2
fi

# Demangled names with sizes. A failed or foreign nm must not pass as an image with nothing in it, so the
# table has to hold a symbol every CrossPoint image has.
if ! syms=$("$nm_bin" -C -S --defined-only "$elf"); then
  echo "nm failed on $elf"
  exit 2
fi
if ! grep -q 'board_tag::boardName' <<<"$syms"; then
  echo "nm output for $elf has no board_tag::boardName: wrong nm or ELF?"
  exit 2
fi
names=$(awk '{ $1 = ""; $2 = ""; $3 = ""; sub(/^ +/, ""); print }' <<<"$syms")

status=0
fail() {
  echo "$1"
  status=1
}

# Touch driver bodies: every InputManager touch function the 4.26" board's image has and a no-touch one
# does not (the controllers' begin and poll, the sleep and hibernate code, the id check, the gesture
# classifiers), and the helpers they use.
touch_bodies='InputManager::(begin(Cst816s|Ft5x06|Ft6336u|Gt911|PiccoTouch|TouchController|Touch)|poll(Cst816s|Ft5x06|Ft6336u|Gslx680|Gt911|PiccoTouch)|serviceTouch|updateTouchFromIrq|focaltechHibernate|gt911(ClearStatus|ReadReg|Sleep)|ft5x06(ReadReg|WriteReg)|prepareTouchForSleep|pulseTouchReset|holdTouchIntForSleep|decodeChsc6xFrame|readChsc6xPoint|touchDiagnostics|[A-Za-z]*MultiTouch[A-Za-z]*)[(]|(^|[^A-Za-z0-9_])(focalTechIdentified|cachedGt911Asleep|cachedTouchProbe|driveGt911IntLow|cstTouchInterrupt)[(]'
if hits=$(grep -E "$touch_bodies" <<<"$names"); then
  fail "Touch driver code is linked into ${env}:"
  echo "$hits" | head -20
fi

# Entry points the SDK leaves as stubs when FREEINK_CAP_TOUCH=0 (about 15 and 7 bytes); a body is hundreds.
stub_cap() {  # name max_bytes
  local line size
  line=$(grep -F -- "$1" <<<"$syms" | head -1)
  [ -z "$line" ] && return 0
  size=$((16#$(awk '{print $2}' <<<"$line")))
  if [ "$size" -gt "$2" ]; then
    fail "$1 is $size bytes in ${env} (a stub is at most $2): the touch code is built"
  fi
}
stub_cap 'InputManager::startTouch()' 64
stub_cap 'InputManager::resetAndHibernateTouch()' 32

# The other panel families: the variant links the X3 pair and nothing of the SSD1677, the UC8279 X4
# driver or the generic probe (its own stock X3 probe, probeX3DisplayController, stays).
other_panels='freeink::(Ssd1677Driver|ssd1677[A-Za-z]*|Uc8279X4Driver|uc8279X4[A-Za-z]*|Uc8179Driver|uc8179[A-Za-z]*)|runDisplayProbePass|probeDisplayController|matchUc81xx|verIsFloating|epdCmdRead|epdReadByte'
if hits=$(grep -E "$other_panels" <<<"$names"); then
  fail "SSD1677, UC8279 X4 or generic display-probe code is linked into ${env}:"
  echo "$hits" | head -20
fi

for driver in 'freeink::Uc8253X3Driver::' 'freeink::Uc8279Driver::'; do
  count=$(grep -c -F -- "$driver" <<<"$names" || true)
  if [ "${count:-0}" -lt 10 ]; then
    fail "${driver} has ${count:-0} symbols in ${env}, expected the X3 driver linked"
  fi
done

expected_tag="silkscreen-368"
case "$env" in
  *-test)
    expected_tag="silkscreen-368-test"
    if hits=$(grep -E 'testsuite::[A-Za-z(): ]*(TouchMappingTest|TouchSleepTest)::' <<<"$names"); then
      fail "The T11 / T12 touch tests are linked into ${env}:"
      echo "$hits" | head -10
    fi
    if ! grep -q -F 'testsuite::NoTouchTest::' <<<"$names"; then
      fail "The no-touch stub of T11 / T12 (testsuite::NoTouchTest) is not linked into ${env}"
    fi
    ;;
esac

# One tag, the variant's own: OTA and the SD update refuse the 4.26" board's image by it.
tags=$(LC_ALL=C grep -a -o -E 'CROSSPOINT-BOARD-V1:[!-:<-~]{1,23};' "$bin" || true)
count=$(printf '%s' "$tags" | grep -c . || true)
if [ "$count" -ne 1 ] || [ "$tags" != "CROSSPOINT-BOARD-V1:${expected_tag};" ]; then
  fail "Expected exactly one board tag CROSSPOINT-BOARD-V1:${expected_tag}; in ${bin}, found ${count}:"
  printf '%s\n' "$tags"
fi

if [ "$status" -eq 0 ]; then
  echo "${env}: no touch driver, no SSD1677 / UC8279 X4 / generic probe code, the X3 drivers linked; board tag ${expected_tag} present once."
fi
exit "$status"

#!/usr/bin/env bash
# No-touch audit of a built Silkscreen 3.68" image (docs/silkscreen/MODULARITY.md, "The 3.68" variant").
#
# The variant has no touch at all: no tail on J4, FREEINK_CAP_TOUCH=0, the board's profile with NO_TOUCH.
# Nothing may then probe 0x38, 0x5D or 0x14, reset TP_RST or send a hibernate at a sleep entry, and the
# build proves it by what is linked. This reads the image's symbol table and fails when:
#   - a touch driver body is linked (the FT5x06 / FT6336U / GT911 / CST816S / GSLX680 / Picco / CHSC6x
#     begin, poll and sleep code, the FocalTech id check, the hibernate and the multi-touch classifiers),
#     or InputManager::resetAndHibernateTouch() is not the stub (7 bytes here, 255 with the driver);
#   - the UC8253 X3 driver (Uc8253X3Driver and its LUTs), the SSD1677 or UC8279 X4 driver, or any display
#     probe (the stock X3 VER read, probeX3DisplayController, or the two-pass VER / FLG / MTP read) is
#     linked: the variant's panel is the X3's UC8279d, fixed in its profile, and nothing identifies it;
#   - the UC8279d X3 driver (Uc8279Driver) is not linked, so a pass would prove nothing;
#   - in a test image, the T11 / T12 touch tests are linked, or their no-touch stub is not;
#   - the image's board tag is not the variant's (silkscreen-368, silkscreen-368-test for the test image).
#
# What each check rests on, so a red result is read right. The touch-body name patterns match symbols that a
# no-touch build still compiles (InputManager::beginTouch(), serviceTouch(), prepareTouchForSleep(), the
# *MultiTouch* classifiers, ...): they are out of the image only because nothing references them and the
# linker's --gc-sections drops them (and, for beginTouch(), because it is inlined). An -O0 / -Og build, or an
# app or SDK change that reaches one of them, fails this audit on an image with no touch driver; that is a
# false alarm to look at, not touch code. The other direction does not occur: with the driver built,
# beginTouch() is 268 bytes and out of line. The one size cap is resetAndHibernateTouch(), the stub that
# discriminates. InputManager::startTouch() is 15 bytes in a touch build too (it only calls beginTouch()), so
# it carries no cap.
#
# Usage: scripts/silkscreen/check_no_touch.sh [env]    (default: silkscreen-368; build it first)
# Exits 0 on a pass, 1 when a check fails (each is listed), 2 when the image or nm cannot be read.
# The nm binary comes from $NM, else from PlatformIO's Xtensa toolchain. ELF=<file> and BIN=<file> name
# another image than .pio/build/<env>/firmware.{elf,bin}: the negative controls are this script on the 4.26"
# board's silkscreen and silkscreen-test images, which must fail on the touch driver, the SSD1677 driver and
# the missing UC8279d driver, and on a 3.68" image built before the UC8279d-only change (the UC8253 driver
# and the stock probe linked), which must fail on those two.
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

# The entry point whose size tells the two builds apart: the SDK leaves resetAndHibernateTouch() a 7-byte stub
# when FREEINK_CAP_TOUCH=0, and it is 255 bytes with the driver (the 4.26" board's image). startTouch() is not
# capped: it is a 15-byte forwarder either way.
stub_cap() {  # name max_bytes
  local line size
  line=$(grep -F -- "$1" <<<"$syms" | head -1)
  [ -z "$line" ] && return 0
  size=$((16#$(awk '{print $2}' <<<"$line")))
  if [ "$size" -gt "$2" ]; then
    fail "$1 is $size bytes in ${env} (a stub is at most $2): the touch code is built"
  fi
}
stub_cap 'InputManager::resetAndHibernateTouch()' 32

# The other panels' code and every probe: the variant links the X3's UC8279d driver and nothing of the
# UC8253 X3 driver, the SSD1677, the UC8279 X4 driver, the stock X3 VER read (probeX3DisplayController) or
# the generic two-pass probe. Its controller is fixed in its profile and nothing identifies it at a boot
# or a wake.
other_panels='freeink::(Ssd1677Driver|ssd1677[A-Za-z]*|Uc8253X3Driver|uc8253X3[A-Za-z]*|Uc8279X4Driver|uc8279X4[A-Za-z]*|Uc8179Driver|uc8179[A-Za-z]*)|probeX3DisplayController|runDisplayProbePass|probeDisplayController|matchUc81xx|verIsFloating|epdCmdRead|epdReadByte'
if hits=$(grep -E "$other_panels" <<<"$names"); then
  fail "The UC8253 X3 driver, the SSD1677 or UC8279 X4 driver, or display-probe code (the stock X3 read, the two-pass read) is linked into ${env}:"
  echo "$hits" | head -20
fi

driver='freeink::Uc8279Driver::'
count=$(grep -c -F -- "$driver" <<<"$names" || true)
if [ "${count:-0}" -lt 10 ]; then
  fail "${driver} has ${count:-0} symbols in ${env}, expected the X3 UC8279d driver linked"
fi

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
  echo "${env}: no touch driver, no UC8253 / SSD1677 / UC8279 X4 driver, no display-probe code, the UC8279d X3 driver linked; board tag ${expected_tag} present once."
fi
exit "$status"

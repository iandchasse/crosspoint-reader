#!/usr/bin/env bash
# Checks a built hardware test firmware (docs/silkscreen/TEST-SUITE.md):
# - no reader, library, book-parsing, web-server, OTA or SD-update code and no
#   light panel is linked into the image, so it cannot open a book;
# - the image carries exactly one board tag, CROSSPOINT-BOARD-V1:<env>;, so the
#   reader's OTA and SD update reject it as another board's image.
# Usage: scripts/check_test_firmware.sh [env]   (default: silkscreen-test; build it first)
# The nm binary comes from $NM, else from PlatformIO's Xtensa toolchain.
set -u
cd "$(dirname "$0")/.." || exit 2

env="${1:-silkscreen-test}"
elf=".pio/build/${env}/firmware.elf"
bin=".pio/build/${env}/firmware.bin"
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

status=0

# Class and function names (demangled), each preceded by a non-identifier
# character so TestSleepActivity does not match SleepActivity.
forbidden='(^|[^A-Za-z0-9_])(ReaderActivity|EpubReaderActivity|XtcReaderActivity|HomeActivity|LibraryListActivity|FileBrowserActivity|BmpViewerActivity|OpdsBookBrowserActivity|SleepActivity|FrontlightPanelActivity|SettingsActivity|SdFirmwareUpdateActivity|OtaUpdateActivity|CrossPointWebServerActivity|CrossPointWebServer|WebDAVHandler|OtaUpdater|Section|ChapterHtmlSlimParser|BookMetadataCache)::|(^|[^A-Za-z0-9_])(Epub|Xtc)::load|[^A-Za-z0-9_]XML_ParserCreate'
if hits=$("$nm_bin" -C "$elf" | grep -E "$forbidden"); then
  echo "Reader, web-server, update or light-panel code is linked into ${env}:"
  echo "$hits" | head -40
  status=1
fi

expected="CROSSPOINT-BOARD-V1:${env};"
tags=$(LC_ALL=C grep -a -o -E 'CROSSPOINT-BOARD-V1:[!-:<-~]{1,23};' "$bin" || true)
count=$(printf '%s' "$tags" | grep -c . || true)
if [ "$count" -ne 1 ] || [ "$tags" != "$expected" ]; then
  echo "Expected exactly one board tag ${expected} in ${bin}, found ${count}:"
  printf '%s\n' "$tags"
  status=1
fi

if [ "$status" -eq 0 ]; then
  echo "${env}: no reader code linked; board tag ${expected} present once."
fi
exit "$status"

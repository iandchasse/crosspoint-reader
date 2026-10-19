#!/usr/bin/env bash
# Checks a built hardware test firmware (docs/silkscreen/TEST-SUITE.md):
# - no reader, library, book-parsing, web-server, OTA, SD-update, plugin or Bluetooth
#   radio code and no light panel is linked into the image, so it cannot open a book
#   or start a radio;
# - no static initialiser from reader code runs at boot, except the allowed
#   ones below: PlatformIO links every object of lib/Epub and lib/LibraryIndex
#   (shared headers pull them in), and --gc-sections keeps their initialisers
#   through .init_array, with the data those reference (the hyphenation tries);
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

# nm's own status, and a symbol every image has: a failed or foreign nm must
# not pass as an image with nothing in it.
if ! syms=$("$nm_bin" -C "$elf"); then
  echo "nm failed on $elf"
  exit 2
fi
if ! raw=$("$nm_bin" "$elf"); then
  echo "nm failed on $elf"
  exit 2
fi
if ! grep -q 'board_tag::boardName' <<<"$syms"; then
  echo "nm output for $elf has no board_tag::boardName: wrong nm or ELF?"
  exit 2
fi

# Class and function names (demangled), each preceded by a non-identifier
# character so TestSleepActivity does not match SleepActivity.
forbidden='(^|[^A-Za-z0-9_])(ReaderActivity|EpubReaderActivity|XtcReaderActivity|HomeActivity|LibraryListActivity|FileBrowserActivity|BmpViewerActivity|OpdsBookBrowserActivity|CatalogActivity|PluginCatalogActivity|SleepActivity|FrontlightPanelActivity|SettingsActivity|SdFirmwareUpdateActivity|OtaUpdateActivity|CrossPointWebServerActivity|CrossPointWebServer|WebDAVHandler|OtaUpdater|Section|ChapterHtmlSlimParser|BookMetadataCache|pluginevents|pluginhttp|PluginLocations|NimBLE[A-Za-z0-9]*|BleKeyboardHost|BleSettingsActivity|SettingsListActivity|PluginHubInstallActivity|DetailsActivity|HttpDownloader|OpdsClient)::|(^|[^A-Za-z0-9_])(Epub|Xtc)::load|[^A-Za-z0-9_]XML_ParserCreate'
if hits=$(grep -E "$forbidden" <<<"$syms"); then
  echo "Reader, web-server, update, plugin, Bluetooth or light-panel code is linked into ${env}:"
  echo "$hits" | head -40
  status=1
fi

# Static initialisers (raw names: nm -C leaves _GLOBAL__sub_I_<mangled> as it
# is) from reader code. The allowed ones set up buffers, pointers and the
# hyphenation tables, or (since 2026-10-19) only create a mutex or register an
# atexit for an empty string: lib/Epub's Epub.cpp (the GIF generation mutex),
# BookMetadataCache.cpp (the size-window mutex) and the SDK Opds lib's
# OpdsFeedParser.cpp (kNoValue); any other is new and needs a look.
reader_init='_GLOBAL__sub_I_.*(CrossPointWebServer|WebDAV|Hyphenat|ImageBlock|ImageDecoder|[0-9]library|Library|Epub|Xtc|Section|ChapterHtml|BookMetadata|Opds|KOReader|Reader|Plugin|Catalog)'
allowed_init='_GLOBAL__sub_I__Z34getLanguageHyphenatorForPrimaryTag|_GLOBAL__sub_I__ZN10ImageBlockC2|_GLOBAL__sub_I__ZN19ImageDecoderFactory11jpegDecoderE|_GLOBAL__sub_I__ZN7library16libraryIndexPathEv|_GLOBAL__sub_I__ZN4EpubC2ENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEERKS5_|_GLOBAL__sub_I__ZN17BookMetadataCache10beginWriteEv|_GLOBAL__sub_I__ZN14OpdsFeedParser20setPreferredLanguageEPKc'
if hits=$(grep -E "$reader_init" <<<"$raw" | grep -v -E "$allowed_init"); then
  echo "A static initialiser from reader code runs in ${env} (not in the allow-list):"
  echo "$hits" | head -40
  status=1
fi
allowed_count=$(grep -E "$reader_init" <<<"$raw" | grep -c -E "$allowed_init" || true)

expected="CROSSPOINT-BOARD-V1:${env};"
tags=$(LC_ALL=C grep -a -o -E 'CROSSPOINT-BOARD-V1:[!-:<-~]{1,23};' "$bin" || true)
count=$(printf '%s' "$tags" | grep -c . || true)
if [ "$count" -ne 1 ] || [ "$tags" != "$expected" ]; then
  echo "Expected exactly one board tag ${expected} in ${bin}, found ${count}:"
  printf '%s\n' "$tags"
  status=1
fi

if [ "$status" -eq 0 ]; then
  echo "${env}: no reader code linked (${allowed_count} allowed lib/Epub and lib/LibraryIndex static initialisers); board tag ${expected} present once."
fi
exit "$status"

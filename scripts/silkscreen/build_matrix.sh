#!/usr/bin/env bash
# Silkscreen build matrix for crosspoint-reader (docs/silkscreen/UPSTREAM-SYNC.md).
#
# Usage: scripts/silkscreen/build_matrix.sh [--full] [--label NAME] [env ...]
#   no envs : silkscreen silkscreen-batlog silkscreen-test silkscreen-368 silkscreen-368-batlog
#             silkscreen-368-test x4pro c3check
#   --full  : adds silkscreen-gh_release, silkscreen-368-gh_release, x4c, papermono and stickycheck
# Prints one line per env (exit code, flash and RAM bytes, log path) and exits non-zero if any
# env or check failed. Logs go to .cache/silkscreen-builds/ (gitignored).
#
# Order matters:
#   1. scripts/check_sleep_wake_sources.sh (static guard).
#   2. The real envs, from platformio.ini (silkscreen-batlog is the battery log's image; the
#      silkscreen-368* envs are the 3.68" X3-panel variant's: docs/silkscreen/MODULARITY.md).
#   3. The image checks, for each env of those that was built: scripts/check_test_firmware.sh for
#      silkscreen-test and silkscreen-368-test, and scripts/silkscreen/check_no_touch.sh for every
#      silkscreen-368* env. They read .pio/build/<env>, which step 4 deletes. The same audit then
#      runs on the 4.26" silkscreen image, which it must fail on the touch names, the other panels'
#      names, the touch stub's size and the missing UC8279d driver: the control that its patterns
#      still match a real touch build (skipped when that image is not on disk).
#   4. The proxy envs, LAST. `default` and `sticky` extend [firmware_tuned], whose custom
#      sdkconfig rebuild cannot run on the Windows bench host (pioarduino tool-scons vs
#      PlatformIO Core), so c3check / stickycheck build their flags and sources on [base]
#      from a copy of platformio.ini. A different config file changes PlatformIO's project
#      checksum, and PlatformIO then wipes ALL of .pio/build, including any build or upload
#      the user has running in this checkout.
#
# Never run this while the user is building or flashing from this checkout; it refuses to start while any pio
# process runs (exit 3).
set -u
SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
cd "$(dirname "$0")/../.." || exit 2
REPO=$(pwd)
# Refuse to start while any PlatformIO run is going: the user's own build or upload, or a build an earlier job left
# behind (on Windows, stopping a shell can leave its pio children running). Two runs in one checkout corrupt
# .pio/build, and the proxy envs below delete it outright.
if tasklist //FI "IMAGENAME eq pio.exe" 2>/dev/null | grep -qi "pio.exe"; then
  echo "a pio process is already running (a build or an upload): wait for it, or end it, then retry" >&2
  tasklist //FI "IMAGENAME eq pio.exe" 2>/dev/null | grep -i "pio.exe" >&2
  exit 3
fi
LOGS="$REPO/.cache/silkscreen-builds"
mkdir -p "$LOGS"
export PYTHONIOENCODING=utf-8 PYTHONUTF8=1 CROSSPOINT_RC_HASH="${CROSSPOINT_RC_HASH:-local}"
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.longpaths GIT_CONFIG_VALUE_0=true

label=$(date +%Y%m%d-%H%M%S)
full=0
envs=()
while [ $# -gt 0 ]; do
  case "$1" in
    --full) full=1 ;;
    --label) shift; label="$1" ;;
    -h|--help) awk 'NR > 1 && /^#/ { print; next } NR > 1 { exit }' "$SELF"; exit 0 ;;
    *) envs+=("$1") ;;
  esac
  shift
done
if [ ${#envs[@]} -eq 0 ]; then
  envs=(silkscreen silkscreen-batlog silkscreen-test silkscreen-368 silkscreen-368-batlog silkscreen-368-test x4pro c3check)
  [ $full = 1 ] && envs=(silkscreen silkscreen-gh_release silkscreen-batlog silkscreen-test silkscreen-368 \
    silkscreen-368-gh_release silkscreen-368-batlog silkscreen-368-test x4pro x4c papermono c3check stickycheck)
fi

real=()
proxy=()
for e in "${envs[@]}"; do
  case "$e" in
    c3check|stickycheck) proxy+=("$e") ;;
    default|sticky) echo "$e: not buildable on this host; use c3check / stickycheck" >&2; exit 2 ;;
    *) real+=("$e") ;;
  esac
done

PROXY="$LOGS/proxy-platformio.ini"
cp platformio.ini "$PROXY"
cat >> "$PROXY" <<'EOF'

[env:c3check]
extends = base
build_flags =
  ${base.build_flags}
  -DFREEINK_DEVICE_X4=1
  -DFREEINK_DEVICE_X3=1
  -DCROSSPOINT_VERSION=\"c3check\"
  -DENABLE_SERIAL_LOG
  -DCROSSPOINT_WAIT_FOR_USB_SERIAL
  -DLOG_LEVEL=2

[env:stickycheck]
extends = base
board = esp32-s3-devkitc1-n16r8
board_build.mcu = esp32s3
build_flags =
  ${base.build_flags}
  -DFREEINK_DEVICE_STICKY=1
  -DCROSSPOINT_VERSION=\"stickycheck\"
  -DENABLE_SERIAL_LOG
  -DCROSSPOINT_WAIT_FOR_USB_SERIAL
  -DLOG_LEVEL=2
EOF

failed=0
report() {  # env rc log
  local flash ram
  flash=$(grep -oE "Flash: \[[^]]*\] +[0-9.]+% \(used [0-9]+ bytes" "$3" | grep -oE "used [0-9]+" | tail -1)
  ram=$(grep -oE "RAM: +\[[^]]*\] +[0-9.]+% \(used [0-9]+ bytes" "$3" | grep -oE "used [0-9]+" | tail -1)
  echo "$1 exit=$2 flash=${flash#used } ram=${ram#used } log=$3"
  if [ "$2" -ne 0 ]; then grep -m8 -E "error:|Error " "$3" | sed 's/^/  /'; failed=1; fi
}

echo "SDK HEAD $(git -C freeink-sdk rev-parse --short HEAD), gitlink $(git ls-tree HEAD freeink-sdk | awk '{print substr($3,1,7)}')"
bash scripts/check_sleep_wake_sources.sh > "$LOGS/$label-sleepguard.log" 2>&1
rc=$?; echo "sleep-wake guard exit=$rc: $(tail -1 "$LOGS/$label-sleepguard.log")"; [ $rc -ne 0 ] && failed=1

for env in "${real[@]}"; do
  log="$LOGS/$label-$env.log"
  pio run -e "$env" > "$log" 2>&1
  report "$env" $? "$log"
done

for env in "${real[@]}"; do
  case "$env" in
    silkscreen-test) log="$LOGS/$label-testfw.log" ;;
    silkscreen-368-test) log="$LOGS/$label-testfw-368.log" ;;
    *) continue ;;
  esac
  bash scripts/check_test_firmware.sh "$env" > "$log" 2>&1
  rc=$?; echo "test firmware check ($env) exit=$rc: $(tail -1 "$log")"; [ $rc -ne 0 ] && failed=1
done

# The 3.68" variant has no touch: the image must carry no touch driver and none of the other panel
# families' code, and its own board tag.
audited=0
for env in "${real[@]}"; do
  case "$env" in
    silkscreen-368*)
      log="$LOGS/$label-notouch-$env.log"
      bash scripts/silkscreen/check_no_touch.sh "$env" > "$log" 2>&1
      rc=$?; echo "no-touch audit ($env) exit=$rc: $(tail -1 "$log")"; [ $rc -ne 0 ] && failed=1
      audited=1
      ;;
  esac
done
# The audit's control. Its patterns are names, and a name that matches nothing passes: if the SDK renames a
# driver function, a 3.68" image that links the touch driver or another panel's driver goes green. The 4.26"
# board's image links the touch driver, the SSD1677 and UC8279 X4 drivers and a 255-byte
# resetAndHibernateTouch() (no 3.68" image links that symbol, so only this run shows its size cap live), and
# it links no UC8279d driver, so the same audit on it must fail on each of those. Run only after an audit
# above, and only when that image is on disk (silkscreen builds first in the default list).
if [ $audited = 1 ]; then
  if [ -f .pio/build/silkscreen/firmware.elf ] && [ -f .pio/build/silkscreen/firmware.bin ]; then
    log="$LOGS/$label-notouch-control.log"
    ELF=.pio/build/silkscreen/firmware.elf BIN=.pio/build/silkscreen/firmware.bin \
      bash scripts/silkscreen/check_no_touch.sh silkscreen-368 > "$log" 2>&1
    rc=$?
    if [ $rc -eq 1 ] && grep -q "^Touch driver code is linked" "$log" \
        && grep -q "^A UC8253, SSD1677, UC8279 X4 or UC8179 driver, or display-probe code" "$log" \
        && grep -q "^InputManager::resetAndHibernateTouch() is [0-9]* bytes" "$log" \
        && grep -q "^freeink::Uc8279Driver:: has 0 symbols" "$log"; then
      echo "no-touch audit control (4.26\" silkscreen image): fails on the touch names, the other panels' names, the touch stub's size and the missing UC8279d driver, as it must"
    else
      echo "no-touch audit control (4.26\" silkscreen image) exit=$rc: did NOT fail on all of the touch names, the other" \
        "panels' names, the touch stub's size and the missing UC8279d driver, so the audit's patterns no longer match" \
        "a touch build: see $log"
      failed=1
    fi
  else
    echo "no-touch audit control skipped: no .pio/build/silkscreen image (build the silkscreen env first)"
  fi
fi

if [ ${#proxy[@]} -gt 0 ]; then
  echo "proxy envs next: they wipe .pio/build (all envs) when they start"
fi
for env in "${proxy[@]}"; do
  log="$LOGS/$label-$env.log"
  pio run -c "$PROXY" -e "$env" > "$log" 2>&1
  report "$env" $? "$log"
done

exit $failed

#!/usr/bin/env bash
# Silkscreen upstream status (docs/silkscreen/UPSTREAM-SYNC.md, step 0).
#
# Usage: scripts/silkscreen/upstream_status.sh [--no-fetch]
# Fetches every upstream, then reports what a sync would take and whether the pins agree:
#   crosspoint-reader (this repo)  : branch silkscreen vs upstream/develop
#   crosspoint-reader/freeink-sdk  : branch silkscreen, the one shared Silkscreen SDK line, vs the
#                                    SDK commit upstream CrossPoint pins and Free-Ink main
#   ../crossink                    : branch silkscreen vs crossink/development; its SDK gitlink must
#                                    be the shared head, and CrossInk's own pin is classified
#   battery log                    : the silkscreen-batlog env, the SDK's BatteryLog files and every app hook (CrossPoint
#                                    and CrossInk) must still be there: a merge can drop a hook without breaking a build
#   render watchdog                : the unit, its hooks in ActivityManager.cpp and main.cpp, the shared sleep and the source
#                                    guard, in both apps (POWER.md AW-04): a merge that drops a hook builds every env and
#                                    leaves the watchdog silently off
#   3.68" variant                  : the SDK's SILKSCREEN_PANEL_368 macro, profile (the UC8279d, fixed and forced) and driver
#                                    selection, the strip upload's transaction fix (upstream's, ungated: txn.end() before
#                                    PARTIAL_OUT, no gate in that function), the BUSY_N pull-up's release before sleep, the
#                                    silkscreen-368 envs and their board tags, the app's panel predicate and its sites,
#                                    the no-touch audit and its control, and in CrossInk its device type, key gate and
#                                    panel sites (a dropped predicate site builds and then ghosts the first page, as a hook
#                                    does)
#   ../../de-link_pcb              : informational only (the PCB repo is the user's)
# Exit status: 0 nothing to do, 1 a merge or a pin fix is needed (lines marked "!"), 2 error.
# Read-only apart from `git fetch`.
set -u
cd "$(dirname "$0")/../.." || exit 2
CP=$(pwd)
SDK="$CP/freeink-sdk"
ROOT=$(cd "$CP/.." && pwd)
CI="$ROOT/crossink"
PCB=$(cd "$ROOT/.." && pwd)/de-link_pcb
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.longpaths GIT_CONFIG_VALUE_0=true

fetch=1
[ "${1:-}" = "--no-fetch" ] && fetch=0
need=0
flag() { printf '  ! %s\n' "$*"; need=1; }
info() { printf '  - %s\n' "$*"; }
short() { git -C "$1" rev-parse --short "$2" 2>/dev/null; }
clean() { [ -z "$(git -C "$1" status --porcelain --ignore-submodules=dirty 2>/dev/null)" ]; }

if [ $fetch = 1 ]; then
  git -C "$CP" fetch -q upstream || { echo "git fetch upstream failed in $CP" >&2; exit 2; }
  git -C "$SDK" fetch -q fk-upstream || { echo "git fetch fk-upstream failed in $SDK" >&2; exit 2; }
  if [ -e "$CI/.git" ]; then
    git -C "$CI" fetch -q crossink || { echo "git fetch crossink failed in $CI" >&2; exit 2; }
    git -C "$CI/freeink-sdk" fetch -q silkscreen-sdk 2>/dev/null
    git -C "$CI/freeink-sdk" fetch -q origin 2>/dev/null
  fi
  [ -e "$PCB/.git" ] && git -C "$PCB" fetch -q --all 2>/dev/null
fi

echo "crosspoint-reader ($(git -C "$CP" branch --show-current) $(short "$CP" HEAD))"
clean "$CP" || flag "working tree not clean: commit or stash before a merge"
read -r ahead behind < <(git -C "$CP" rev-list --left-right --count silkscreen...upstream/develop)
info "ahead of upstream/develop by $ahead, behind by $behind ($(short "$CP" upstream/develop))"
[ "$behind" -gt 0 ] && flag "merge upstream/develop: $(git -C "$CP" log --oneline silkscreen..upstream/develop | wc -l) commits"
pin=$(git -C "$CP" ls-tree upstream/develop freeink-sdk | awk '{print $3}')
ours=$(git -C "$CP" ls-tree HEAD freeink-sdk | awk '{print $3}')
info "upstream pins freeink-sdk ${pin:0:7}; our gitlink ${ours:0:7}"

echo "freeink-sdk shared line ($(git -C "$SDK" branch --show-current) $(short "$SDK" HEAD))"
clean "$SDK" || flag "working tree not clean"
[ "$(git -C "$SDK" rev-parse HEAD)" = "$(git -C "$SDK" rev-parse silkscreen)" ] || flag "submodule HEAD is not the silkscreen branch head"
[ "$ours" = "$(git -C "$SDK" rev-parse silkscreen)" ] || flag "crosspoint-reader gitlink ${ours:0:7} is not the shared head $(short "$SDK" silkscreen)"
if git -C "$SDK" merge-base --is-ancestor "$pin" silkscreen 2>/dev/null; then
  info "upstream's pin ${pin:0:7} is already merged"
else
  flag "merge upstream's SDK pin ${pin:0:7} into silkscreen first (step 1)"
fi
read -r _ fkbehind < <(git -C "$SDK" rev-list --left-right --count silkscreen...fk-upstream/main)
info "Free-Ink main is $fkbehind commit(s) ahead of the shared line ($(short "$SDK" fk-upstream/main)); the line follows upstream CrossPoint's pin, not main"

if [ -e "$CI/.git" ]; then
  echo "crossink ($(git -C "$CI" branch --show-current) $(short "$CI" HEAD))"
  clean "$CI" || flag "working tree not clean"
  read -r ciahead cibehind < <(git -C "$CI" rev-list --left-right --count silkscreen...crossink/development)
  info "ahead of crossink/development by $ciahead, behind by $cibehind ($(short "$CI" crossink/development))"
  [ "$cibehind" -gt 0 ] && flag "merge crossink/development (step 3)"
  cigl=$(git -C "$CI" ls-tree HEAD freeink-sdk | awk '{print $3}')
  shared=$(git -C "$SDK" rev-parse silkscreen)
  if [ "$cigl" = "$shared" ]; then
    info "SDK gitlink ${cigl:0:7} is the shared head"
  else
    flag "SDK gitlink ${cigl:0:7} is not the shared head ${shared:0:7}: move it (step 4)"
  fi
  cipin=$(git -C "$CI" ls-tree crossink/development freeink-sdk | awk '{print $3}')
  if git -C "$SDK" merge-base --is-ancestor "$cipin" silkscreen 2>/dev/null; then
    info "crossink/development pins ${cipin:0:7}: already on the shared line (keep the shared head)"
  elif git -C "$SDK" merge-base --is-ancestor "$cipin" fk-upstream/main 2>/dev/null; then
    flag "crossink/development pins ${cipin:0:7}, a Free-Ink main commit not yet on the shared line: merge it into the shared line first"
  else
    info "crossink/development pins ${cipin:0:7}, on no Free-Ink main (a pre-merge PR pin): keep the shared head"
  fi
else
  echo "crossink: not found at $CI"
fi

# The battery log (the silkscreen-batlog env): a build passes with a hook dropped, and the log just stops recording, so
# look for the env, the SDK files and each hook by name. The minimums are the counts of call sites there today.
echo "battery log (silkscreen-batlog: the env, the SDK files and the hooks must survive every merge)"
blmiss=0
batlog_need() {  # dir file pattern min: the file holds at least min lines matching the pattern
  local n
  n=$(grep -c -- "$3" "$1/$2" 2>/dev/null)
  if [ "${n:-0}" -lt "$4" ]; then
    flag "battery log: $2 in $(basename "$1") has ${n:-0} line(s) matching '$3', expected at least $4"
    blmiss=$((blmiss + 1))
  fi
}
batlog_file() {  # dir file
  if [ ! -f "$1/$2" ]; then
    flag "battery log: $2 is missing from $(basename "$1")"
    blmiss=$((blmiss + 1))
  fi
}
SDKPM=libs/hardware/PowerManager
for f in include/BatteryLogCore.h include/BatteryLog.h src/BatteryLog.cpp test/host/test_batterylog_core.cpp; do
  batlog_file "$SDK" "$SDKPM/$f"
done
batlog_need "$SDK" "$SDKPM/src/PowerManager.cpp" 'batterylog::tail' 1
batlog_file "$CP" scripts/silkscreen/battery_log_report.py
batlog_file "$CP" scripts/silkscreen/test_battery_log_report.py
batlog_app() {  # dir clock-sync-file early-calls tag-calls
  local dir=$1 sym
  batlog_need "$dir" platformio.ini '^\[env:silkscreen-batlog\]' 1
  batlog_need "$dir" platformio.ini 'DSILKSCREEN_BATLOG=1' 1
  batlog_need "$dir" scripts/silkscreen/build_matrix.sh 'silkscreen-batlog' 1
  batlog_file "$dir" src/util/BatteryLogWriter.h
  batlog_file "$dir" src/util/BatteryLogWriter.cpp
  for sym in 'batterylog::beginEntry' 'batterylog::onWake' 'batterylog::carry' 'batterylog::noteInput' \
             'batterylog::tick(' 'BatteryLogWriter::flushIfDue' 'BatteryLogWriter::flushAtEntry'; do
    batlog_need "$dir" src/main.cpp "$sym" 1
  done
  batlog_need "$dir" src/main.cpp 'batterylog::early' "$3"
  batlog_need "$dir" src/main.cpp 'batterylog::tagPath' "$4"
  batlog_need "$dir" lib/hal/HalPowerManager.cpp 'batterylog::noteUiSeed' 1
  batlog_need "$dir" lib/hal/HalPowerManager.cpp 'batterylog::noteFlag' 1
  batlog_need "$dir" lib/hal/HalGPIO.cpp 'batterylog::noteFlag' 1
  batlog_need "$dir" "$2" 'batterylog::noteClockSynced' 1
}
batlog_app "$CP" lib/TrustedTime/TrustedTime.cpp 4 4
if [ -e "$CI/.git" ]; then
  batlog_app "$CI" lib/hal/HalClock.cpp 2 6
  batlog_need "$CI" scripts/git_branch.py "'silkscreen-batlog'" 2
fi
[ $blmiss = 0 ] && info "the env, the SDK files, both writers and every hook are present"

# The render watchdog (docs/silkscreen/POWER.md, AW-04), in both apps. Its hooks sit in upstream's ActivityManager.cpp and
# main.cpp, and a merge that loses one still builds every env: the watchdog is then silently off (or a restart-into-sleep
# lands at the wrong point). Counts the hooks, and runs the source guard, which reads their order and the lock-free
# expiry path (test_render_watchdog.py --source-only; check_render_watchdog_elf.py reads the linked image and needs a build).
echo "render watchdog (AW-04: the unit, its hooks in ActivityManager.cpp, main.cpp and the long renders, the shared sleep, the log hold, the guards)"
rwdmiss=0
# Every hook is gated by the SDK's FREEINK_CAP_POWER_POLICY (Silkscreen only): a merge that drops or renames that macro
# turns all of them off at once and every env still builds.
if ! grep -q 'define FREEINK_CAP_POWER_POLICY (FREEINK_DEVICE_SILKSCREEN)' "$SDK/libs/hardware/BoardConfig/include/BoardConfig.h" 2>/dev/null; then
  flag "render watchdog: the SDK's BoardConfig.h no longer has 'define FREEINK_CAP_POWER_POLICY (FREEINK_DEVICE_SILKSCREEN)': every watchdog hook is gated by it"
  rwdmiss=$((rwdmiss + 1))
fi
rwd_need() {  # dir file pattern min: the file holds at least min lines matching the pattern
  local n
  n=$(grep -c -- "$3" "$1/$2" 2>/dev/null)
  if [ "${n:-0}" -lt "$4" ]; then
    flag "render watchdog: $2 in $(basename "$1") has ${n:-0} line(s) matching '$3', expected at least $4"
    rwdmiss=$((rwdmiss + 1))
  fi
}
rwd_file() {  # dir file
  if [ ! -f "$1/$2" ]; then
    flag "render watchdog: $2 is missing from $(basename "$1")"
    rwdmiss=$((rwdmiss + 1))
  fi
}
rwd_app() {  # dir epub-min home-min txt-min: the least number of watchdogProgress() calls in each long render's file
  local dir=$1 f
  for f in src/util/RenderWatchdog.h src/util/RenderWatchdog.cpp scripts/silkscreen/test_render_watchdog.py \
           scripts/silkscreen/render_watchdog_host_test.cpp scripts/silkscreen/check_render_watchdog_elf.py; do
    rwd_file "$dir" "$f"
  done
  rwd_need "$dir" scripts/silkscreen/build_matrix.sh 'test_render_watchdog.py' 1
  rwd_need "$dir" scripts/silkscreen/build_matrix.sh 'check_render_watchdog_elf.py' 1
  rwd_need "$dir" src/activities/ActivityManager.cpp 'renderwatchdog::beginRender(' 1
  rwd_need "$dir" src/activities/ActivityManager.cpp 'renderwatchdog::endRender()' 1
  rwd_need "$dir" src/activities/ActivityManager.cpp 'renderwatchdog::start()' 1
  rwd_need "$dir" src/main.cpp 'renderwatchdog::noteLoopPass()' 1
  rwd_need "$dir" src/main.cpp 'renderwatchdog::beginSleepEntry()' 1
  rwd_need "$dir" src/main.cpp 'renderwatchdog::resetLoopSlept()' 1
  rwd_need "$dir" src/main.cpp 'renderwatchdog::holdsLogs()' 1
  rwd_need "$dir" src/main.cpp 'holdLastLogs(true)' 1
  rwd_need "$dir" src/main.cpp 'holdLastLogs(false)' 1
  rwd_need "$dir" src/main.cpp 'renderwatchdog::takeSleepRequest(' 1
  rwd_need "$dir" src/main.cpp 'renderwatchdog::takeReport(' 1
  rwd_need "$dir" src/main.cpp 'renderwatchdog::describe(' 2
  rwd_need "$dir" src/main.cpp '^static void sleepWithoutDrivers()' 1
  rwd_need "$dir" src/main.cpp '^static void sleepIfRenderWatchdog()' 1
  rwd_need "$dir" src/main.cpp '^static void logWatchdogReport(' 1
  rwd_need "$dir" src/main.cpp '^ *sleepIfRenderWatchdog();' 1
  rwd_need "$dir" src/main.cpp 'sleepWithoutDrivers();' 2
  rwd_need "$dir" lib/Logging/Logging.cpp 'holdLastLogs' 1
  rwd_need "$dir" lib/Logging/Logging.cpp 'logRingHeld' 3
  rwd_need "$dir" lib/Logging/Logging.h 'holdLastLogs' 1
  # The long renders that re-stamp (renderwatchdog::progress() through each file's watchdogProgress()).
  rwd_need "$dir" src/activities/reader/EpubReaderActivity.cpp '^ *watchdogProgress();' "$2"
  rwd_need "$dir" src/activities/home/HomeActivity.cpp '^ *watchdogProgress();' "$3"
  [ "$4" -gt 0 ] && rwd_need "$dir" src/activities/reader/TxtReaderActivity.cpp '^ *watchdogProgress();' "$4"
  if command -v python > /dev/null 2>&1 && [ -f "$dir/scripts/silkscreen/test_render_watchdog.py" ]; then
    if ! (cd "$dir" && python -I -X utf8 scripts/silkscreen/test_render_watchdog.py --source-only > /dev/null 2>&1); then
      flag "render watchdog: test_render_watchdog.py --source-only fails in $(basename "$dir"): run it for the reason"
      rwdmiss=$((rwdmiss + 1))
    fi
  fi
}
rwd_app "$CP" 5 1 0
if [ -e "$CI/.git" ]; then
  rwd_app "$CI" 6 2 1
fi
[ $rwdmiss = 0 ] && info "the unit, the hooks, the shared sleep, the log hold, the re-stamps and the guards are present in both apps"

# The 3.68" X3-panel variant (-DSILKSCREEN_PANEL_368=1, the silkscreen-368 envs; docs/silkscreen/MODULARITY.md). A merge
# that drops the SDK's profile or driver selection, an env, a board tag or one of the panel predicate's sites still
# builds every env: the variant then boots the wrong driver, shares the 4.26" board's tag, or ghosts its first page.
echo "3.68\" variant (silkscreen-368: the SDK's profile, selection, strip-transaction fix and BUSY_N release, the envs and tags, the panel predicate and its sites)"
v368miss=0
v368_need() {  # dir file pattern min: the file holds at least min lines matching the pattern
  local n
  n=$(grep -c -- "$3" "$1/$2" 2>/dev/null)
  if [ "${n:-0}" -lt "$4" ]; then
    flag "3.68\" variant: $2 in $(basename "$1") has ${n:-0} line(s) matching '$3', expected at least $4"
    v368miss=$((v368miss + 1))
  fi
}
v368_atmost() {  # dir file pattern max: the file holds at most max lines matching the pattern
  local n
  n=$(grep -c -- "$3" "$1/$2" 2>/dev/null)
  if [ "${n:-0}" -gt "$4" ]; then
    flag "3.68\" variant: $2 in $(basename "$1") has ${n:-0} line(s) matching '$3', expected at most $4"
    v368miss=$((v368miss + 1))
  fi
}
v368_file() {  # dir file
  if [ ! -f "$1/$2" ]; then
    flag "3.68\" variant: $2 is missing from $(basename "$1")"
    v368miss=$((v368miss + 1))
  fi
}
v368_in_section() {  # dir file section pattern min: [section] of an ini file holds at least min non-comment matching lines
  local n
  n=$(awk -v sec="$3" -v pat="$4" '$0 ~ ("^\\[" sec "\\]") { s = 1; next } /^\[/ { s = 0 } s && !/^[[:space:]]*;/ && $0 ~ pat { c++ } END { print c + 0 }' "$1/$2" 2>/dev/null)
  if [ "${n:-0}" -lt "$5" ]; then
    flag "3.68\" variant: [$3] in $2 of $(basename "$1") has ${n:-0} line(s) matching '$4', expected at least $5"
    v368miss=$((v368miss + 1))
  fi
}
SDKBC=libs/hardware/BoardConfig/include/BoardConfig.h
v368_need "$SDK" "$SDKBC" '^#define FREEINK_SILKSCREEN_368 ' 1
v368_need "$SDK" "$SDKBC" '^#define SILKSCREEN_PANEL_368 0' 1
v368_need "$SDK" "$SDKBC" 'SILKSCREEN_368 = ' 1
v368_need "$SDK" "$SDKBC" '^namespace silkscreen_368' 1
# The variant links the X3's UC8279d driver and not the UC8253 one: that driver's macro is the C3 X3's alone, so
# the variant's own condition is on the UC8279 line only (a second such line would link the UC8253 driver).
v368_need "$SDK" "$SDKBC" '^#if FREEINK_DEVICE_X3 || FREEINK_SILKSCREEN_368' 1
v368_atmost "$SDK" "$SDKBC" '^#if FREEINK_DEVICE_X3 || FREEINK_SILKSCREEN_368' 1
v368_need "$SDK" "$SDKBC" 'FREEINK_DEVICE_SILKSCREEN && !SILKSCREEN_PANEL_368' 2
# ... and fixes its controller in data: the UC8279d, a forced probe policy, no verdict cache.
v368_need "$SDK" "$SDKBC" 'p.displayController = DisplayController::UC8279;' 1
v368_need "$SDK" "$SDKBC" 'p.displayProbe.forced = true;' 1
v368_need "$SDK" "$SDKBC" 'p.displayProbe.cacheAcrossDeepSleep = false;' 1
for f in libs/hardware/XteinkDetect/src/XteinkDetect.cpp libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp \
         libs/display/FreeInkDisplay/src/bus/EpdBus.cpp libs/hardware/PowerManager/src/PowerManager.cpp; do
  v368_need "$SDK" "$f" 'FREEINK_SILKSCREEN_368' 1
done
# XteinkDetect's own block for the variant (the forced log line is also the 4.26" board's, so two) and its test.
v368_need "$SDK" libs/hardware/XteinkDetect/src/XteinkDetect.cpp 'controller forced by the build' 2
v368_file "$SDK" libs/hardware/XteinkDetect/test/host/test_silkscreen_368_policy.cpp
v368_file "$SDK" libs/hardware/BoardConfig/test/host/run_profile_asserts.py
v368_need "$SDK" libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp 'setDisplayX3();' 1
# The strip upload's transaction fix. Uc8279Driver::writeGrayscalePlaneStrip() must end its bus Transaction before
# bus.cmd(CMD_PARTIAL_OUT): held across that command it blocks on arduino-esp32's non-recursive SPI mutex (a frozen
# device, no panic). The fix is upstream's (Free-Ink PR #139, c2d711a, merged as 7fcd3b3 on 2026-10-12), ungated, and
# every UC8279d build has it. This line carried a gated copy (4988be4) until b460001 reverted it to take upstream's text,
# so the check is that the function ends the transaction first and carries no FREEINK_SILKSCREEN_368 (or any other)
# gate: a later merge that takes an older file, or a gate that comes back, builds every env and freezes the first
# grayscale image page.
strip_fix=$(awk '
  /Uc8279Driver::writeGrayscalePlaneStrip\(/ { fn = 1 }
  fn {
    if ($0 ~ /FREEINK_SILKSCREEN_368/ || $0 ~ /^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif|else)/) gate = 1
    if (!ended && $0 ~ /^[[:space:]]*txn\.end\(\);/) ended = NR
    if (!partialOut && $0 ~ /bus\.cmd\(CMD_PARTIAL_OUT\)/) partialOut = NR
    opened = gsub(/\{/, "{"); closed = gsub(/\}/, "}")
    depth += opened - closed
    if (opened + closed > 0 && depth == 0) { fn = 0; seen = 1 }
  }
  END {
    if (!seen) print "writeGrayscalePlaneStrip() not found"
    else if (!partialOut) print "no bus.cmd(CMD_PARTIAL_OUT) in writeGrayscalePlaneStrip()"
    else if (!ended) print "no txn.end() in writeGrayscalePlaneStrip()"
    else if (ended > partialOut) print "txn.end() comes after bus.cmd(CMD_PARTIAL_OUT) in writeGrayscalePlaneStrip()"
    else if (gate) print "a preprocessor gate (FREEINK_SILKSCREEN_368 or another) is in writeGrayscalePlaneStrip()"
    else print "ok"
  }' "$SDK/libs/display/FreeInkDisplay/src/driver/Uc8279Driver.cpp" 2>/dev/null)
if [ "${strip_fix:-}" != "ok" ]; then
  flag "3.68\" variant: Uc8279Driver.cpp: ${strip_fix:-unreadable}: upstream's strip-transaction fix (Free-Ink #139) must stand, ungated"
  v368miss=$((v368miss + 1))
fi
# The BUSY_N pull-up is for the awake panel only: powerDownRailsForSleep() strips both pulls and leaves a plain input
# before every sleep (the IDF isolates the unheld pad at sleep entry too, so this is the second line; a pull-up left
# on against a LOW BUSY_N costs 73 uA). A rename of busyPullUp, or a dropped block, passes every build and test.
v368_need "$SDK" libs/hardware/PowerManager/src/PowerManager.cpp 'BoardConfig::displayBusyPullUp()' 1
v368_need "$SDK" libs/hardware/PowerManager/src/PowerManager.cpp 'gpio_pullup_dis(busy);' 1
v368_variant_app() {  # dir [sites]: what every app that has the variant carries (CrossInk names its envs and has no test firmware)
  local dir=$1
  # HalDisplay.cpp's HALF -> resync and gray-base sites: five in both apps (displayBuffer, displayBufferAsync,
  # refreshDisplay and the two displayGrayscaleBase overloads). A merge that resolves one of them back to
  # gpio.deviceIsX3() builds every env and ghosts that path on the variant.
  local sites=${2:-5}
  v368_need "$dir" lib/hal/HalDisplay.h 'isX3Panel' 1
  v368_need "$dir" lib/hal/HalDisplay.h 'FREEINK_SILKSCREEN_368' 1
  v368_need "$dir" lib/hal/HalDisplay.cpp 'isX3Panel()' "$sites"
  v368_need "$dir" src/main.cpp 'isX3Panel()' 1
  v368_need "$dir" src/network/FirmwareBoardTag.cpp '"silkscreen-368"' 1
  v368_need "$dir" platformio.ini '^\[env:silkscreen-368\]' 1
  v368_need "$dir" platformio.ini 'DSILKSCREEN_PANEL_368=1' 1
  v368_need "$dir" scripts/silkscreen/build_matrix.sh 'silkscreen-368' 1
  # The audit's control (it runs the audit on the 4.26" silkscreen image, which must fail): without it a pattern
  # that matches nothing passes.
  v368_need "$dir" scripts/silkscreen/build_matrix.sh 'notouch-control.log' 1
}
v368_variant_app "$CP"
v368_need "$CP" src/network/FirmwareBoardTag.cpp '"silkscreen-368-test"' 1
v368_need "$CP" platformio.ini '^\[env:silkscreen-368-gh_release\]' 1
v368_need "$CP" platformio.ini '^\[env:silkscreen-368-test\]' 1
v368_need "$CP" src/activities/boot_sleep/SleepActivity.cpp 'isX3Panel()' 1
v368_need "$CP" src/activities/reader/EpubReaderActivity.cpp 'isX3Panel()' 1
v368_need "$CP" platformio.ini '^\[env:silkscreen-368-gh_release_rc\]' 1
v368_need "$CP" platformio.ini '^\[env:silkscreen-368-batlog\]' 1
v368_need "$CP" platformio.ini '^\[silkscreen368\]' 1
v368_need "$CP" platformio.ini 'DFREEINK_CAP_TOUCH=0' 1
v368_file "$CP" scripts/silkscreen/check_no_touch.sh
v368_need "$CP" scripts/silkscreen/build_matrix.sh 'check_no_touch.sh' 1
v368_need "$CP" .github/workflows/ci.yml 'silkscreen-368' 2
v368_file "$CP" src/activities/testsuite/tests/NoTouchTest.h
v368_file "$CP" src/activities/testsuite/tests/PanelFamily.h
v368_need "$CP" src/activities/testsuite/tests/TouchMappingTest.cpp 'NoTouchTest' 1
v368_need "$CP" src/activities/testsuite/tests/TouchSleepTest.cpp 'NoTouchTest' 1
# The variant must not inherit the GT911 / alternate-jumper flags: no touch_flags reference inside [silkscreen368].
if [ -f "$CP/platformio.ini" ]; then
  n=$(awk '/^\[silkscreen368\]/ { s = 1; next } /^\[/ { s = 0 } s && !/^[[:space:]]*;/ && /touch_flags/ { c++ } END { print c + 0 }' "$CP/platformio.ini")
  if [ "$n" -ne 0 ]; then
    flag "3.68\" variant: [silkscreen368] in platformio.ini refers to touch_flags $n time(s): the variant has no touch tail"
    v368miss=$((v368miss + 1))
  fi
fi
if [ -e "$CI/.git" ]; then
  if grep -q '^\[env:silkscreen-368\]' "$CI/platformio.ini" 2>/dev/null; then
    v368_variant_app "$CI"
    # What only CrossInk's port carries (it has no test firmware, and it names its envs and its OTA device type):
    # the battery-log env and its version suffix (the analyzer reads "-silkscreen-batlog" as CrossInk's), the
    # variant's own device type (type "silkscreen" would match the variant's asset in the 4.26" board's OTA), the
    # app's no-touch macro and no touch_flags, the key gate and the sites that ask the panel.
    v368_need "$CI" platformio.ini '^\[env:silkscreen-368-batlog\]' 1
    v368_need "$CI" platformio.ini '^custom_firmware_device_type = silkscreen368' 2
    v368_in_section "$CI" platformio.ini silkscreen368 'DCROSSINK_APP_CAP_TOUCH=0' 1
    v368_in_section "$CI" platformio.ini silkscreen368 'FIRMWARE_DEVICE_TYPE=.*silkscreen368' 1
    v368_need "$CI" scripts/git_branch.py "'-368-silkscreen-batlog'" 1
    v368_file "$CI" scripts/silkscreen/check_no_touch.sh
    v368_need "$CI" include/DeviceCapabilities.h '^#elif FREEINK_SILKSCREEN_368' 1
    # The ten deviceHasX3Panel() sites: v368_variant_app counts the HalDisplay ones and one in main.cpp only.
    v368_need "$CI" src/main.cpp 'deviceHasX3Panel()' 2
    v368_need "$CI" src/activities/ScreenTransitionRefresh.h 'deviceHasX3Panel()' 1
    v368_need "$CI" src/activities/boot_sleep/SleepActivity.cpp 'deviceHasX3Panel()' 1
    v368_need "$CI" src/components/themes/dashboard/DashboardTheme.cpp 'deviceHasX3Panel()' 3
    v368_need "$CI" src/components/themes/minimal/MinimalTheme.cpp 'deviceHasX3Panel()' 1
    v368_need "$CI" src/SettingsList.cpp 'deviceHasX3Panel()' 1
    v368_need "$CI" src/network/CrossPointWebServer.cpp 'deviceHasX3Panel()' 1
    if [ -f "$CI/platformio.ini" ]; then
      n=$(awk '/^\[silkscreen368\]/ { s = 1; next } /^\[/ { s = 0 } s && !/^[[:space:]]*;/ && /touch_flags/ { c++ } END { print c + 0 }' "$CI/platformio.ini")
      if [ "$n" -ne 0 ]; then
        flag "3.68\" variant: [silkscreen368] in crossink's platformio.ini refers to touch_flags $n time(s): the variant has no touch tail"
        v368miss=$((v368miss + 1))
      fi
    fi
  else
    flag "3.68\" variant: crossink has no [env:silkscreen-368]: port it (PORTING.md, drain checklist item 15)"
    v368miss=$((v368miss + 1))
  fi
fi
[ $v368miss = 0 ] && info "the SDK's macro, profile and selection, the strip upload's transaction fix (upstream's, ungated), the BUSY_N release before sleep, "\
"the envs, the tags, the predicate and its sites, the audit and its control, and CrossInk's own pieces are present"

if [ -e "$PCB/.git" ]; then
  up=$(git -C "$PCB" rev-parse --abbrev-ref '@{upstream}' 2>/dev/null)
  echo "de-link_pcb ($(git -C "$PCB" branch --show-current) $(short "$PCB" HEAD)), informational"
  if [ -n "$up" ]; then
    read -r pa pb < <(git -C "$PCB" rev-list --left-right --count "HEAD...$up")
    info "ahead of $up by $pa, behind by $pb"
  fi
  clean "$PCB" || info "uncommitted changes (the user's; leave them)"
fi

if [ $need = 0 ]; then echo "STATUS: current"; else echo "STATUS: work to do (lines marked !)"; fi
exit $need

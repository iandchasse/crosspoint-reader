#!/usr/bin/env bash
# Run ONE command under the shared PlatformIO build lock (docs/silkscreen/UPSTREAM-SYNC.md).
#
# Usage: scripts/silkscreen/pio_locked.sh <label> <command...>     (run from the repo whose pio you call)
#
# Why: pioarduino's ComponentManager rewrites the SHARED package ~/.platformio/packages/framework-arduinoespressif32-libs
# while a build runs, so two pio runs at once (even in two checkouts) can end on "esp_bt.h: No such file". Every pio
# call of every job takes the lock directory silkscreen-firmware/.pio-build.lock first (mkdir is atomic), writes who
# holds it into owner.txt, and removes it when the command ends (a failure included). It waits while another job holds
# it or any pio.exe runs. A lock older than 90 minutes with no pio.exe running is stale and is taken over.
# The command runs with SILKSCREEN_LOCK_HELD=1, so a script that is itself run under the lock does not wait on itself.
set -u
LABEL="$1"; shift
HERE="$(cd "$(dirname "$0")" && pwd)"
LOCK="$(cd "$HERE/../../.." && pwd)/.pio-build.lock"
if [ "${SILKSCREEN_LOCK_HELD:-}" = 1 ]; then exec "$@"; fi

held=0
release() {
  if [ "$held" = 1 ]; then
    rm -f "$LOCK/owner.txt"
    rmdir "$LOCK" 2>/dev/null
    held=0
    echo "[$(date +%H:%M:%S)] lock released"
  fi
}
trap release EXIT INT TERM HUP
pio_running() { tasklist //FI "IMAGENAME eq pio.exe" 2>/dev/null | grep -qi "pio.exe"; }

while true; do
  if mkdir "$LOCK" 2>/dev/null; then
    held=1
    if pio_running; then
      echo "[$(date +%H:%M:%S)] a pio.exe is running although the lock was free: waiting for it"
      release
    else
      break
    fi
  else
    if [ -d "$LOCK" ] && find "$LOCK" -maxdepth 0 -mmin +90 2>/dev/null | grep -q . && ! pio_running; then
      echo "[$(date +%H:%M:%S)] the lock is over 90 minutes old and no pio.exe runs: stale, taking it over"
      rm -f "$LOCK/owner.txt"; rmdir "$LOCK" 2>/dev/null
      continue
    fi
    echo "[$(date +%H:%M:%S)] waiting for the build lock: $(head -c 160 "$LOCK/owner.txt" 2>/dev/null)"
  fi
  sleep 30
done

echo "$LABEL | $(pwd) | $* | started $(date -u '+%Y-%m-%d %H:%M:%S') UTC" > "$LOCK/owner.txt"
echo "[$(date +%H:%M:%S)] build lock taken: $LABEL"
export SILKSCREEN_LOCK_HELD=1 PYTHONUTF8=1 PYTHONIOENCODING=utf-8
"$@"
rc=$?
echo "[$(date +%H:%M:%S)] command exit=$rc"
exit $rc

#!/usr/bin/env bash
# Silkscreen upstream sync, lean path (docs/silkscreen/UPSTREAM-SYNC.md).
#
# Usage: scripts/silkscreen/sync.sh [options]
#   Brings the shared SDK line and CrossPoint up to their upstreams. CrossInk is not in this path yet: its sync is the
#   reference procedure (UPSTREAM-SYNC-REFERENCE.md, step 3), run after this one moved the shared SDK head.
# Options:
#   --envs "a b"  the envs to build (default: silkscreen-batlog silkscreen silkscreen-368)
#   --also "x y"  more envs after those (the bench firmware: silkscreen-test silkscreen-368-test)
#   --no-merge    do not merge: re-run the checks, the census, the builds and the freeze on the tree as it is
#   --no-build    stop after the merge, the checks and the census
#   --no-freeze   do not copy the images into ../flash/<env>/
#   --dry-run     fetch, say what is due and which files would conflict; change nothing
#   --accept      after the census items were read: write census_baseline.txt and a docs/silkscreen/SYNC-LOG.md entry
#                 and commit both (needs the last green run to be on this HEAD)
#   --note "..."  with --accept: what the reviewer found and did, one or two sentences, for the log
#
# What it does (nothing else; no matrix, no clean builds, no proxy envs):
#   status -> SDK merge -> CrossPoint merge -> guards -> census of what came in -> builds (warm, one env at a time under
#   the shared build lock) -> freeze the images -> one page of results with the minutes each phase took.
# Exit status: 0 green and nothing to read; 11 green, the census lists items to read (see the page); 10 a merge stopped on
# conflicts (resolve, commit, run the script again: it carries on); 12 a guard or a build failed; 2 cannot start.
# Commits stay local. It never pushes.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
CP="${SYNC_CP:-$(cd "$HERE/../.." && pwd)}"
SDK="$CP/freeink-sdk"
ROOT="$(cd "$CP/.." && pwd)"
LOGS="$CP/.cache/silkscreen-builds"
mkdir -p "$LOGS"
export CENSUS_ROOT="$CP"
export PYTHONUTF8=1 PYTHONIOENCODING=utf-8 GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.longpaths GIT_CONFIG_VALUE_0=true

envs=""; also=""; do_merge=1; do_build=1; do_freeze=1; dry=0; accept=0; note=""
while [ $# -gt 0 ]; do
  case "$1" in
    crosspoint) ;;
    crossink|all) echo "sync: CrossInk is not in the lean path yet; see UPSTREAM-SYNC-REFERENCE.md step 3" >&2; exit 2 ;;
    --envs) shift; envs="$1" ;;
    --also) shift; also="$1" ;;
    --no-merge) do_merge=0 ;;
    --no-build) do_build=0 ;;
    --no-freeze) do_freeze=0 ;;
    --dry-run) dry=1 ;;
    --accept) accept=1 ;;
    --note) shift; note="$1" ;;
    -h|--help) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 0 ;;
    *) echo "unknown argument: $1 (see --help)" >&2; exit 2 ;;
  esac
  shift
done

T0=$(date +%s)
TIMES=()
PH=""; PHT=0
phase() { phase_end; PH="${2:-$1}"; PHT=$(date +%s); printf '\n== %s\n' "$1"; }
phase_end() { [ -n "$PH" ] && TIMES+=("$PH $(( $(date +%s) - PHT ))s"); PH=""; }
die() { echo "sync: $*" >&2; exit 2; }
short() { git -C "$1" rev-parse --short "$2" 2>/dev/null; }
fmt_s() { printf '%dm%02ds' $(( $1 / 60 )) $(( $1 % 60 )); }

RESULTS="$LOGS/last-sync.txt"
STATE_OK=1      # 0 once a guard or a build failed
CENSUS_N=0      # census lines to read
CONFLICT=0
MERGED=""       # what moved, for the log

# ---------------------------------------------------------------------------------------------------------------------
if [ $accept = 1 ]; then
  [ -f "$RESULTS" ] || die "no last run recorded ($RESULTS): run the sync first"
  # shellcheck disable=SC1090
  . "$RESULTS"
  [ "$(git -C "$CP" rev-parse HEAD)" = "$LAST_HEAD" ] || die "HEAD moved since the last run ($LAST_HEAD): run --no-merge again so the log describes this tree"
  [ "$LAST_OK" = 1 ] || die "the last run was not green: fix it, run --no-merge, then --accept"
  [ -z "$(git -C "$CP" status --porcelain --ignore-submodules=all)" ] || die "crosspoint-reader is not clean: commit the fixes first"
  python -I -X utf8 "$HERE/sync_census.py" accept HEAD || exit 2
  rm -f "$LOGS/sync-state.txt"
  LOGF="$CP/docs/silkscreen/SYNC-LOG.md"
  [ -f "$LOGF" ] || printf '# Silkscreen sync log\n\nOne entry per sync, written by `scripts/silkscreen/sync.sh --accept`: what moved, what the census listed, what the builds took.\nThe procedure is [UPSTREAM-SYNC.md](UPSTREAM-SYNC.md); older, hand-written records are in [PORTING.md](PORTING.md#upstream-merges).\n' > "$LOGF"
  {
    printf '\n## %s  %s\n\n' "$(date +%Y-%m-%d)" "$LAST_MOVED"
    printf '%s\n' "$LAST_DETAIL"
    printf -- '- Reviewed: %s\n' "${note:-nothing to fix (census read, no finding)}"
  } >> "$LOGF"
  git -C "$CP" add "$LOGF" "$CP/scripts/silkscreen/census_baseline.txt"
  git -C "$CP" commit -q -m "docs(silkscreen): sync log $(date +%Y-%m-%d)" -m "Census baseline accepted; entry written by scripts/silkscreen/sync.sh --accept." \
    && echo "committed: $(short "$CP" HEAD) docs(silkscreen): sync log $(date +%Y-%m-%d)"
  exit 0
fi

# ---------------------------------------------------------------------------------------------------------------------
phase "status"
[ "$(git -C "$CP" branch --show-current)" = silkscreen ] || die "crosspoint-reader is not on silkscreen"
[ "$(git -C "$SDK" branch --show-current)" = silkscreen ] || die "freeink-sdk is not on silkscreen"
git -C "$CP" rev-parse -q --verify MERGE_HEAD >/dev/null && die "crosspoint-reader is in the middle of a merge: resolve and commit it, then run this script again"
git -C "$SDK" rev-parse -q --verify MERGE_HEAD >/dev/null && die "freeink-sdk is in the middle of a merge: resolve and commit it, then run this script again"
if [ $do_merge = 1 ] || [ $dry = 0 ]; then
  [ -z "$(git -C "$CP" status --porcelain --ignore-submodules=all)" ] || { [ $dry = 1 ] || die "crosspoint-reader is not clean: commit or stash first"; echo "  (dry run: the tree is not clean)"; }
  [ -z "$(git -C "$SDK" status --porcelain)" ] || { [ $dry = 1 ] || die "freeink-sdk is not clean: commit or stash first"; echo "  (dry run: the SDK tree is not clean)"; }
fi
git -C "$CP" fetch -q upstream || die "git fetch upstream failed"
git -C "$SDK" fetch -q fk-upstream || die "git fetch fk-upstream failed"

cp_behind=$(git -C "$CP" rev-list --count silkscreen..upstream/develop)
pin=$(git -C "$CP" ls-tree upstream/develop freeink-sdk | awk '{print $3}')
if git -C "$SDK" merge-base --is-ancestor "$pin" silkscreen 2>/dev/null; then sdk_due=0; else sdk_due=1; fi
sdk_commits=$(git -C "$SDK" rev-list --count silkscreen.."$pin" 2>/dev/null || echo 0)
echo "  CrossPoint: $cp_behind upstream commit(s) due (upstream/develop $(short "$CP" upstream/develop))"
echo "  SDK: upstream CrossPoint pins $(short "$SDK" "$pin"); $([ $sdk_due = 1 ] && echo "NOT yet on the shared line (+$sdk_commits commits)" || echo "already on the shared line")"
shared=$(git -C "$SDK" rev-parse silkscreen)
cp_gl=$(git -C "$CP" ls-tree HEAD freeink-sdk | awk '{print $3}')
[ "$cp_gl" = "$shared" ] || echo "  note: CrossPoint's gitlink $(short "$SDK" "$cp_gl") is not the shared head $(short "$SDK" "$shared")"

if [ $do_merge = 1 ] && [ "$cp_behind" -gt 0 ]; then
  echo "  predicted conflicts in CrossPoint (git merge-tree, nothing is changed):"
  git -C "$CP" merge-tree --write-tree --name-only --no-messages HEAD upstream/develop 2>/dev/null | sed '1d' | sed '/^$/d' | sed 's/^/    /' | head -30
fi
if [ $dry = 1 ]; then phase_end; echo; echo "dry run: nothing changed"; exit 0; fi
if [ $do_merge = 1 ] && [ $sdk_due = 0 ] && [ "$cp_behind" -eq 0 ] && [ "$cp_gl" = "$shared" ]; then
  echo "  nothing to merge: the checks, the census and the builds run on the tree as it is"
fi

# The heads before the first merge of this sync: the census and the conditional guards compare against them, also when the
# run is repeated after a conflict was resolved (the state file carries them across the runs; --accept removes it).
STATE="$LOGS/sync-state.txt"
if [ -f "$STATE" ] && find "$STATE" -mmin -720 2>/dev/null | grep -q .; then
  # shellcheck disable=SC1090
  . "$STATE"
  echo "  resuming the sync that started at CrossPoint ${PRE_CP:0:8}, SDK ${PRE_SDK:0:7}"
else
  PRE_CP=$(git -C "$CP" rev-parse HEAD)
  PRE_SDK=$(git -C "$SDK" rev-parse HEAD)
  if [ $sdk_due = 1 ] || [ "$cp_behind" -gt 0 ]; then
    printf 'PRE_CP=%s
PRE_SDK=%s
' "$PRE_CP" "$PRE_SDK" > "$STATE"
  fi
fi
conflict_exit() {  # repo name
  CONFLICT=1
  echo
  echo "MERGE STOPPED on conflicts in $2. Files:"
  git -C "$1" diff --name-only --diff-filter=U | sed 's/^/  /'
  echo "Resolve them (for each, find the upstream PR in the commit message and read it before choosing: keep upstream's intent,"
  echo "re-apply the Silkscreen hunk on top; docs/silkscreen/UPSTREAM-SYNC-REFERENCE.md lists the hotspots), \`git add\`, \`git commit\`"
  echo "(the prepared message is in .git/MERGE_MSG; add the conflict resolutions to its body), then run scripts/silkscreen/sync.sh again: it carries on from there"
  exit 10
}

# ---------------------------------------------------------------------------------------------------------------------
if [ $do_merge = 1 ]; then
  if [ $sdk_due = 1 ]; then
    phase "SDK merge ($(short "$SDK" "$pin"))"
    body=$(git -C "$SDK" log --no-merges --format='- %h %s' silkscreen.."$pin" | head -40)
    git -C "$SDK" merge --no-ff -m "Merge $(short "$SDK" "$pin") (Free-Ink main) into silkscreen" -m "$body" "$pin" || conflict_exit "$SDK" "freeink-sdk"
    MERGED="SDK +$sdk_commits ($(short "$SDK" "$pin"))"
  fi
  if [ "$cp_behind" -gt 0 ]; then
    phase "CrossPoint merge (upstream/develop $(short "$CP" upstream/develop))"
    # Record the SDK pointer move first: with the shared head already in the gitlink, git keeps it when upstream moves the
    # pin to a commit the shared head contains, so the merge has no gitlink conflict to resolve.
    pre_gl=$(git -C "$CP" ls-tree HEAD freeink-sdk | awk '{print $3}')
    pre_shared=$(git -C "$SDK" rev-parse silkscreen)
    if [ "$pre_gl" != "$pre_shared" ]; then
      git -C "$CP" add freeink-sdk && git -C "$CP" commit -q -m "chore(silkscreen): move the SDK pointer to $(short "$SDK" silkscreen)"         && echo "  gitlink moved to the shared head $(short "$SDK" silkscreen)"
    fi
    body=$(git -C "$CP" log --no-merges --format='- %h %s' silkscreen..upstream/develop | head -60)
    more=$(( $(git -C "$CP" rev-list --no-merges --count silkscreen..upstream/develop) - 60 ))
    [ "$more" -gt 0 ] && body="$body
- ... and $more more"
    git -C "$CP" merge --no-ff -m "Merge upstream develop ($(short "$CP" upstream/develop)) into silkscreen" -m "$body" upstream/develop       || conflict_exit "$CP" "crosspoint-reader"
    MERGED="${MERGED:+$MERGED, }CrossPoint +$cp_behind (upstream/develop $(short "$CP" upstream/develop))"
  fi
  # the gitlink is the shared head, whatever the merges did to it
  now_gl=$(git -C "$CP" ls-tree HEAD freeink-sdk | awk '{print $3}')
  now_shared=$(git -C "$SDK" rev-parse silkscreen)
  if [ "$now_gl" != "$now_shared" ]; then
    git -C "$CP" add freeink-sdk && git -C "$CP" commit -q -m "chore(silkscreen): move the SDK pointer to $(short "$SDK" silkscreen)" && echo "  gitlink moved to the shared head $(short "$SDK" silkscreen)"
  fi
fi
[ -n "$MERGED" ] || MERGED="no merge (tree as it was)"

# ---------------------------------------------------------------------------------------------------------------------
phase "guards"
guard() {  # label file command...   (file: the script the guard runs; a guard that is not there is a failure)
  local label="$1" file="$2"; shift 2
  local out rc
  if [ -n "$file" ] && [ ! -e "$CP/$file" ]; then
    if [ "${SYNC_ALLOW_MISSING_GUARDS:-}" = 1 ]; then echo "  skip  $label ($file is not in this tree)"
    else echo "  FAIL  $label: $file is MISSING (a merge that drops a guard must not turn the sync green)"; STATE_OK=0; fi
    return
  fi
  out=$("$@" 2>&1); rc=$?
  if [ $rc -eq 0 ]; then echo "  ok    $label: $(printf '%s' "$out" | tail -1 | cut -c1-110)"
  else echo "  FAIL  $label (exit $rc)"; printf '%s
' "$out" | tail -6 | sed 's/^/        /'; STATE_OK=0; fi
}
cd "$CP" || exit 2
guard "sleep and wake sources" scripts/check_sleep_wake_sources.sh bash scripts/check_sleep_wake_sources.sh
guard "render watchdog source" scripts/silkscreen/test_render_watchdog.py python -I -X utf8 scripts/silkscreen/test_render_watchdog.py --source-only
# The lenses (battery log, render watchdog and variant hooks, pins) are upstream_status.sh. Its lines about CrossInk (its
# merge is due, its SDK pointer is not the shared head yet) belong to CrossInk's own sync, so they are listed, not failed.
lens_guard() {
  local file=scripts/silkscreen/upstream_status.sh out rc bad note
  if [ ! -e "$CP/$file" ]; then
    if [ "${SYNC_ALLOW_MISSING_GUARDS:-}" = 1 ]; then echo "  skip  status lenses ($file is not in this tree)"
    else echo "  FAIL  status lenses: $file is MISSING"; STATE_OK=0; fi
    return
  fi
  out=$(bash "$file" --no-fetch 2>&1); rc=$?
  bad=$(printf '%s\n' "$out" | awk '/^[^ ]/ { sec = $1 } /^  ! / { if (sec != "crossink") print }')
  note=$(printf '%s\n' "$out" | awk '/^[^ ]/ { sec = $1 } /^  ! / { if (sec == "crossink") print }')
  if [ "$rc" -ge 2 ] || [ -n "$bad" ]; then
    echo "  FAIL  status lenses (hooks, pins, variant) (exit $rc)"; { [ -n "$bad" ] && printf '%s\n' "$bad" || printf '%s\n' "$out" | tail -6; } | sed 's/^/        /'
    STATE_OK=0
  else
    echo "  ok    status lenses (hooks, pins, variant)"
    [ -n "$note" ] && printf '%s\n' "$note" | sed 's/^  ! /  note  CrossInk (its own sync): /'
  fi
}
lens_guard
# the battery log analyzer compares its tables with BatteryLogCore.h and BatteryMonitor.cpp: a changed curve fails it on purpose
guard "battery log analyzer tests" scripts/silkscreen/test_battery_log_report.py bash -c 'cd scripts/silkscreen && PYTHONDONTWRITEBYTECODE=1 python -X utf8 -m unittest test_battery_log_report 2>&1 | tail -3'
sdk_changed=$(git -C "$SDK" diff --name-only "$PRE_SDK" HEAD 2>/dev/null)
if printf '%s' "$sdk_changed" | grep -q 'BoardConfig'; then
  guard "SDK profile asserts" "" python -I -X utf8 "$SDK/libs/hardware/BoardConfig/test/host/run_profile_asserts.py"
fi
if git -C "$CP" diff --name-only "$PRE_CP" HEAD | grep -qE 'ClipSelectionActivity|src/clippings/|^test/'; then
  guard "app host tests (selection, clipping sync)" scripts/silkscreen/msvc/run_app_host_tests.py python -I -X utf8 scripts/silkscreen/msvc/run_app_host_tests.py
fi
# the cheap one: anything that changed platformio.ini or the lock files rebuilds every env (cold)
if git -C "$CP" diff --name-only "$PRE_CP" HEAD | grep -qE '^(platformio\.ini|sdkconfig|partitions|dependencies\.lock)'; then
  echo "  note  the build configuration changed: expect cold builds (about 15 minutes each) instead of warm ones (about 3)"
fi

# ---------------------------------------------------------------------------------------------------------------------
phase "census (what came in that costs battery, holds a radio, or leaves the keys)" census
python -I -X utf8 "$HERE/sync_census.py" overlap "$PRE_CP" HEAD 2>/dev/null | head -14
python -I -X utf8 "$HERE/sync_census.py" check HEAD > "$LOGS/census.txt" 2>&1
crc=$?
if [ $crc -eq 0 ]; then
  echo "  nothing added or removed since the baseline"
elif [ $crc -eq 1 ]; then
  CENSUS_N=$(head -1 "$LOGS/census.txt" | grep -oE '[0-9]+ surface' | head -2 | tr -dc '0-9 ' | awk '{print $1+$2}')
  head -1 "$LOGS/census.txt"
  awk '/^\[/ { r = substr($1, 2, length($1) - 2) } /^  \+ / { a[r]++; n[r] = 1 } /^  - / { d[r]++; n[r] = 1 }
       END { for (k in n) printf "    %-7s added %d, removed %d\n", k, a[k] + 0, d[k] + 0 }' "$LOGS/census.txt" | sort
  nocut=$(grep -c 'NO cutoff poll' "$LOGS/census.txt")
  [ "$nocut" -gt 0 ] && echo "    $nocut new network function(s) with no low-battery cutoff poll in them"
  echo "  read $LOGS/census.txt ($(grep -c '' "$LOGS/census.txt") lines): those hunks, nothing else"
else
  echo "  census failed: see $LOGS/census.txt"; STATE_OK=0
fi

# ---------------------------------------------------------------------------------------------------------------------
BUILD_LINES=""
if [ $do_build = 1 ] && [ $STATE_OK = 1 ]; then
  [ -n "$envs" ] || envs="silkscreen-batlog silkscreen silkscreen-368"
  all_envs="$envs${also:+ $also}"
  for env in $all_envs; do
    phase "build $env"
    label="sync-$(date +%Y%m%d-%H%M%S)"
    s=$(date +%s)
    bash "$HERE/pio_locked.sh" "sync $env" bash "$HERE/build_matrix.sh" --label "$label" "$env" > "$LOGS/$label-$env.sync.log" 2>&1
    rc=$?
    d=$(( $(date +%s) - s ))
    line=$(grep -E "^$env exit=" "$LOGS/$label-$env.sync.log" | tail -1)
    checks=$(grep -cE "exit=[1-9]" "$LOGS/$label-$env.sync.log")
    if [ $rc -eq 0 ] && [ -n "$line" ]; then
      echo "  $env: $(fmt_s $d)  $(printf '%s' "$line" | sed -E 's/ log=.*//; s/^[^ ]+ //')"
      BUILD_LINES="$BUILD_LINES
- $env: $(fmt_s $d), $(printf '%s' "$line" | sed -E 's/ log=.*//; s/^[^ ]+ //')"
    else
      echo "  $env: FAILED after $(fmt_s $d) (exit $rc): see $LOGS/$label-$env.sync.log"
      grep -m6 -E "error:|Error |exit=[1-9]|MISSING|FAIL" "$LOGS/$label-$env.sync.log" | cut -c1-170 | sed 's/^/        /'
      STATE_OK=0
      BUILD_LINES="$BUILD_LINES
- $env: FAILED"
      break
    fi
  done
  if [ $STATE_OK = 1 ] && [ $do_freeze = 1 ]; then
    phase "freeze images"
    python -I -X utf8 "$HERE/freeze_images.py" $all_envs 2>&1 | sed 's/^/  /'
  fi
fi
phase_end

# ---------------------------------------------------------------------------------------------------------------------
total=$(( $(date +%s) - T0 ))
{
  echo "LAST_HEAD=$(git -C "$CP" rev-parse HEAD)"
  echo "LAST_OK=$STATE_OK"
  echo "LAST_MOVED=\"$MERGED\""
  printf 'LAST_DETAIL=%q\n' "- Merged: $MERGED. Total $(fmt_s $total); census items: $CENSUS_N.${BUILD_LINES:+ Builds:$BUILD_LINES}"
} > "$RESULTS"

echo
echo "================ sync result: $(fmt_s $total) total ($(IFS=,; echo "${TIMES[*]}"))"
echo "  merged:  $MERGED"
echo "  census:  $CENSUS_N item(s) to read"
if [ $STATE_OK = 1 ]; then
  [ $do_build = 1 ] && echo "  builds:  green; images are in $ROOT/flash/"
  if [ "${CENSUS_N:-0}" -gt 0 ]; then
    echo "  next:    read the census hunks, fix what they show (commit), run with --no-merge to rebuild, then --accept --note \"...\""
    exit 11
  fi
  echo "  next:    scripts/silkscreen/sync.sh --accept   (writes the log entry and the census baseline)"
  exit 0
fi
echo "  a guard or a build FAILED: see above"
exit 12

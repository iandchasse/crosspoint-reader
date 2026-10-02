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

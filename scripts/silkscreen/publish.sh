#!/usr/bin/env bash
# Publishes the Silkscreen branches to the owner's forks on GitHub. The owner runs this; no other script pushes.
#
# Usage: scripts/silkscreen/publish.sh [--dry-run]
#   freeink-sdk       silkscreen -> origin (iandchasse/freeink-sdk), branch silkscreen
#   crosspoint-reader silkscreen -> fork   (iandchasse/crosspoint-reader-de-link), branch silkscreen
#   crossink          silkscreen -> fork   (the same repository: CrossInk is in CrossPoint's fork network), branch crossink/silkscreen
# Order matters: the SDK goes first, because a fresh clone resolves both apps' freeink-sdk gitlink in the SDK fork.
# Fast-forward only: nothing is forced, and a rejected push stops the script. A dirty tree, a branch that is not
# `silkscreen`, or a gitlink the SDK branch does not contain stops it before anything is pushed.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
CP="$(cd "$HERE/../.." && pwd)"
SDK="$CP/freeink-sdk"
CI="$(cd "$CP/.." && pwd)/crossink"
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.longpaths GIT_CONFIG_VALUE_0=true
dry=""
case "${1:-}" in --dry-run) dry="--dry-run" ;; "") ;; *) echo "usage: publish.sh [--dry-run]" >&2; exit 2 ;; esac

die() { echo "publish: $*" >&2; exit 2; }
short() { git -C "$1" rev-parse --short "$2" 2>/dev/null; }

for r in "$SDK" "$CP" "$CI"; do
  [ -e "$r/.git" ] || die "$r is not a git checkout"
  [ "$(git -C "$r" branch --show-current)" = silkscreen ] || die "$(basename "$r") is not on silkscreen"
done
[ -z "$(git -C "$SDK" status --porcelain)" ] || die "freeink-sdk is not clean"
[ -z "$(git -C "$CP" status --porcelain --ignore-submodules=all)" ] || die "crosspoint-reader is not clean"
[ -z "$(git -C "$CI" status --porcelain --ignore-submodules=all)" ] || die "crossink is not clean"
git -C "$SDK" remote get-url origin >/dev/null 2>&1 || die "freeink-sdk has no remote origin"
git -C "$CP" remote get-url fork >/dev/null 2>&1 || die "crosspoint-reader has no remote fork"
git -C "$CI" remote get-url fork >/dev/null 2>&1 || die "crossink has no remote fork"

shared=$(git -C "$SDK" rev-parse silkscreen)
for app in "$CP" "$CI"; do
  gl=$(git -C "$app" ls-tree HEAD freeink-sdk | awk '{print $3}')
  git -C "$SDK" merge-base --is-ancestor "$gl" "$shared" || die "$(basename "$app")'s SDK gitlink ${gl:0:7} is not on the SDK's silkscreen branch"
done

push() {  # repo remote src:dst label
  local out
  echo "== $4: $(short "$1" HEAD) -> $2 $3"
  out=$(git -C "$1" push $dry "$2" "$3" 2>&1); local rc=$?
  printf '%s\n' "$out" | grep -vE '^remote: *$|Create a pull request|pull/new' | sed 's/^/   /'
  [ $rc -eq 0 ] || die "$4: the push was rejected or failed (exit $rc); nothing after it was pushed"
}
push "$SDK" origin silkscreen:silkscreen "freeink-sdk"
push "$CP" fork silkscreen:silkscreen "crosspoint-reader"
push "$CI" fork silkscreen:crossink/silkscreen "crossink"
echo "published${dry:+ (dry run)}: https://github.com/iandchasse/freeink-sdk/tree/silkscreen  https://github.com/iandchasse/crosspoint-reader-de-link/tree/silkscreen  https://github.com/iandchasse/crosspoint-reader-de-link/tree/crossink/silkscreen"

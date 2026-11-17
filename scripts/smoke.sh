#!/bin/sh
# What can be checked of a build with no game to hand (CI, `make STUB=1`): the
# program starts - its libraries are found - and tells a folder without the
# game from one with it (exit 66). No window, no sound, and its own save folder.
#   scripts/smoke.sh build/stub/madeline
set -u
BIN=${1:?usage: smoke.sh <the built program>}
T=$(dirname "$BIN")/smoke
mkdir -p "$T/empty"
MADELINE_SAVE_DIR="$T/save" "$BIN" --check "$T/empty"
rc=$?
[ "$rc" = 66 ] || { echo "smoke: expected exit 66 (no game in the folder), got $rc" >&2; exit 1; }
echo "smoke: ok"

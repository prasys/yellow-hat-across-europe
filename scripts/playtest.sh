#!/bin/sh
# Drive the game headless and fast, for testing: scripts/playtest.sh OUTDIR "SCRIPT" [speed]
# SCRIPT is MADELINE_SCRIPT (src/debug.c); `shot NAME` paths are relative to OUTDIR.
# MADELINE_BIN runs another engine than build/madeline (a release's, built by its kit).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$1; SCRIPT=$2; SPEED=${3:-8}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
# MSYS2 (Windows): the game is a native program and is given this path inside
# the script text, where the shell does not translate it: C:/..., not /c/...
if command -v cygpath >/dev/null 2>&1; then OUT=$(cygpath -m "$OUT"); fi
SCRIPT=$(printf '%s' "$SCRIPT" | sed "s#shot #shot $OUT/#g")
# Saves go to OUT/save, never the player's own folder. Put files there first to test loading.
mkdir -p "$OUT/save"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HLE_FILETRACE=1 MADELINE_AUDIOTRACE=1 MADELINE_SPEED=$SPEED MADELINE_NO_NAME=${MADELINE_NO_NAME-1} MADELINE_SAVE_DIR="$OUT/save" \
  MADELINE_SCRIPT="$SCRIPT" "${MADELINE_BIN:-$ROOT/build/madeline}" --headless "${MADELINE_CD:-$ROOT/game/cd}" > "$OUT/run.log" 2>&1 || echo "exit code $?" >> "$OUT/run.log"
# Screenshots to PNG: sips on a Mac, ffmpeg anywhere; left as BMP with neither.
for f in "$OUT"/*.bmp; do
  [ -f "$f" ] || continue
  if command -v sips >/dev/null 2>&1; then sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null 2>&1 && rm "$f"
  elif command -v ffmpeg >/dev/null 2>&1; then ffmpeg -loglevel error -y -i "$f" "${f%.bmp}.png" && rm "$f"; fi
done
# A contact sheet of the shots, in name order (_sheet.png), when ffmpeg is there.
N=0
for f in "$OUT"/*.png; do [ -f "$f" ] && [ "${f##*/}" != _sheet.png ] && N=$((N + 1)); done
if [ "$N" -gt 1 ] && command -v ffmpeg >/dev/null 2>&1; then
  rm -f "$OUT/_sheet.png"
  ROWS=$(( (N + 3) / 4 ))
  ffmpeg -loglevel error -y -pattern_type glob -i "$OUT/[!_]*.png" -vf "scale=320:240,tile=4x$ROWS" -frames:v 1 "$OUT/_sheet.png" || true
fi

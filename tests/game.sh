#!/bin/sh
# With the game (game/cd, or MADELINE_CD) and an engine built for it: start it
# headless, let both opening movies play, and see the main menu come up and
# its music start (by the files opened: no picture is compared), with no
# missing import or function on the way. Needs no input, so it does not
# depend on timing the way a scripted walk does (docs/briefs/03).
# Skipped, not failed, without the game. MADELINE_BIN tests another engine.
#   tests/game.sh
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CD=${MADELINE_CD:-$ROOT/game/cd}
BIN=${MADELINE_BIN:-$ROOT/build/madeline}
[ -f "$BIN" ] || [ -f "$BIN.exe" ] || { echo "game: skipped (no engine at $BIN)"; exit 0; }
[ -f "$CD/MADEURO.EXE" ] || { echo "game: skipped (no game in $CD)"; exit 0; }
OUT=$ROOT/build/test-game
rm -rf "$OUT"
MADELINE_CD=$CD "$ROOT/scripts/playtest.sh" "$OUT" "200000 quit"
LOG=$OUT/run.log
fail=0
for want in 'mad000\mad000.DAT' 'mad000\SCRN000.PCX' 'mad400\meamid4.mid'; do
    grep -qiF "$want" "$LOG" || { echo "game: never opened $want" >&2; fail=1; }
done
if grep -E 'FATAL|unimplemented import|no lifted function|exit code' "$LOG" >&2; then fail=1; fi
[ "$fail" = 0 ] && echo "game: ok (main menu reached)"
[ "$fail" = 0 ] || echo "game: see $LOG" >&2
exit "$fail"

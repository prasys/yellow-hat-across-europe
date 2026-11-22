#!/bin/sh
# The engine and the kit agree on which MADEURO.EXE an engine belongs to. An
# engine linked with the lift_id.c the kit writes for a file accepts that file
# (exit 0), refuses one of the same size with other bytes (3) and one of
# another size (3), and a folder without one (66). The files are random bytes;
# the lift is the empty one of `make STUB=1`, rebuilt here with that lift_id.c.
#   tests/engine_check.sh [python]
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PY=${1:-python3}
B=$ROOT/build/stub
T=$B/test-check
EXE=$B/madeline; [ -f "$EXE.exe" ] && EXE=$EXE.exe
rm -rf "$T" && mkdir -p "$T/disc" "$T/same-size" "$T/other-size" "$T/empty"
head -c 151552 /dev/urandom > "$T/disc/MADEURO.EXE"           # as big as the real one
cp "$T/disc/MADEURO.EXE" "$T/same-size/MADEURO.EXE"
printf 'x' | dd of="$T/same-size/MADEURO.EXE" bs=1 seek=4096 conv=notrunc 2>/dev/null
cmp -s "$T/disc/MADEURO.EXE" "$T/same-size/MADEURO.EXE" && printf 'y' | dd of="$T/same-size/MADEURO.EXE" bs=1 seek=4096 conv=notrunc 2>/dev/null
head -c 151553 /dev/urandom > "$T/other-size/MADEURO.EXE"
"$PY" -c 'import sys; sys.path.insert(0, sys.argv[1]); import madeline_kit as k; k.write_lift_id(sys.argv[2], sys.argv[3])' \
    "$ROOT/kit" "$B/gen" "$T/disc/MADEURO.EXE"
make -C "$ROOT" -s STUB=1 >/dev/null
fail=0
for d in disc:0 same-size:3 other-size:3 empty:66; do
    MADELINE_SAVE_DIR="$T/save" "$EXE" --check "$T/${d%:*}" >/dev/null 2>&1 && rc=0 || rc=$?
    [ "$rc" = "${d#*:}" ] || { echo "engine_check: --check on ${d%:*} gave $rc, expected ${d#*:}" >&2; fail=1; }
done
rm -f "$B/gen/lift_id.c"     # the next `make STUB=1` writes the empty lift's again
make -C "$ROOT" -s STUB=1 >/dev/null
[ "$fail" = 0 ] && echo "engine_check: ok"
exit "$fail"

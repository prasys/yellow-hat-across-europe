#!/bin/sh
# make test: everything that can be checked without a person watching.
#   - the kit's own tests (tests/test_kit.py): checksum, unpacking, the stamp
#   - the stub engine starts (scripts/smoke.sh) and agrees with the kit on
#     which MADEURO.EXE it belongs to (tests/engine_check.sh)
#   - with the game here: it boots to the main menu (tests/game.sh)
#   - with KIT=<a release's kit folder>: that kit builds and starts an engine
#     (madeline_kit.py selftest)
# The Python used needs pycdlib (PYTHON, else the checkout's .venv, which this
# makes as scripts/lift.sh does).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
PY=${PYTHON:-}
if [ -z "$PY" ]; then
    if python3 -c 'import pycdlib' >/dev/null 2>&1; then PY=python3
    else
        [ -x .venv/bin/python ] || python3 -m venv .venv
        PY=.venv/bin/python
        "$PY" -c 'import pycdlib' >/dev/null 2>&1 || "$PY" -m pip install -q pycdlib
    fi
fi
make -s -j4 STUB=1 >/dev/null
EXE=build/stub/madeline; [ -f "$EXE.exe" ] && EXE=$EXE.exe
"$PY" -m unittest discover -s tests -p 'test_*.py'
scripts/smoke.sh "$EXE"
tests/engine_check.sh "$PY"
tests/game.sh
if [ -n "${KIT:-}" ]; then
    T=$(mktemp -d)
    "${KIT_PYTHON:-$KIT/python/bin/python3}" -I -B "$KIT/madeline_kit.py" selftest "$T/engine"
    rm -rf "$T"
fi
echo "test: all passed"

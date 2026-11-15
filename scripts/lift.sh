#!/bin/sh
# Lift MADEURO.EXE from your extracted CD to C with pcrecomp (into build/gen).
#   scripts/lift.sh [game/cd]
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CD=${1:-"$ROOT/game/cd"}
EXE=$(cd "$CD" && pwd)/MADEURO.EXE
[ -f "$EXE" ] || { echo "no $EXE - run scripts/extract_iso.sh first" >&2; exit 66; }
[ -f "$ROOT/third_party/pcrecomp/tools/__main__.py" ] || git -C "$ROOT" submodule update --init
PY="$ROOT/.venv/bin/python"
if [ ! -x "$PY" ]; then
  if command -v uv >/dev/null 2>&1; then uv venv -q "$ROOT/.venv" && uv pip install -q --python "$PY" capstone
  else python3 -m venv "$ROOT/.venv" && "$PY" -m pip install -q capstone; fi
fi
SUM=$(shasum -a 256 "$EXE" | cut -d' ' -f1)
KNOWN=$(cat "$ROOT/docs/MADEURO.sha256" 2>/dev/null || true)
[ -z "$KNOWN" ] || [ "$SUM" = "$KNOWN" ] || echo "warning: MADEURO.EXE differs from the build this port was made against ($SUM)" >&2
mkdir -p "$ROOT/build/gen"
cd "$ROOT/third_party/pcrecomp"
"$PY" -m tools "$EXE" --all -o "$ROOT/build/gen" --functions-json "$ROOT/build/functions.json" | tail -4

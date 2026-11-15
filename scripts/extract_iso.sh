#!/bin/sh
# Unpack your own Madeline: European Adventures CD image into game/cd.
#   scripts/extract_iso.sh /path/to/your.iso [destination]
set -eu
ISO=${1:?usage: extract_iso.sh <your.iso> [destination]}
DEST=${2:-"$(cd "$(dirname "$0")/.." && pwd)/game/cd"}
[ -f "$ISO" ] || { echo "no such file: $ISO" >&2; exit 66; }
mkdir -p "$DEST"
bsdtar -xf "$ISO" -C "$DEST"
chmod -R u+rwX "$DEST"
[ -f "$DEST/MADEURO.EXE" ] || { echo "MADEURO.EXE is not on this disc - is it the right game?" >&2; exit 65; }
echo "extracted to $DEST"

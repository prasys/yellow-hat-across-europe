#!/bin/sh
# Unpack your own Madeline: European Adventures CD image into game/cd.
#   scripts/extract_iso.sh /path/to/your.iso [destination]
set -eu
ISO=${1:?usage: extract_iso.sh <your.iso> [destination]}
DEST=${2:-"$(cd "$(dirname "$0")/.." && pwd)/game/cd"}
[ -f "$ISO" ] || { echo "no such file: $ISO" >&2; exit 66; }
mkdir -p "$DEST"
# bsdtar (libarchive) reads ISO 9660: it is part of macOS; on Linux, libarchive-tools. 7z also works.
if command -v bsdtar >/dev/null 2>&1; then bsdtar -xf "$ISO" -C "$DEST"
elif command -v 7z >/dev/null 2>&1; then 7z x -y -o"$DEST" "$ISO" >/dev/null
else echo "need bsdtar (libarchive-tools) or 7z to unpack a disc image" >&2; exit 69; fi
chmod -R u+rwX "$DEST"
# Windows (MSYS2): the libarchive there has been seen to list this disc as
# empty without an error. Windows reads disc images itself: mount it, copy the
# files, take it out again.
if [ ! -f "$DEST/MADEURO.EXE" ] && command -v powershell.exe >/dev/null 2>&1 && command -v cygpath >/dev/null 2>&1; then
  ISO_W=$(cygpath -w "$(cd "$(dirname "$ISO")" && pwd)/$(basename "$ISO")") DEST_W=$(cygpath -w "$DEST") \
  powershell.exe -NoProfile -Command '
    $ErrorActionPreference = "Stop"
    $img = Mount-DiskImage -ImagePath $env:ISO_W -PassThru
    try {
      $drive = ($img | Get-Volume).DriveLetter
      robocopy "${drive}:\" $env:DEST_W /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
      attrib -R "$env:DEST_W\*" /S /D | Out-Null
    } finally { Dismount-DiskImage -ImagePath $env:ISO_W | Out-Null }'
fi
[ -f "$DEST/MADEURO.EXE" ] || { echo "MADEURO.EXE is not on this disc - is it the right game?" >&2; exit 65; }
echo "extracted to $DEST"

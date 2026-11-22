#!/bin/sh
# The Windows release: a zip with the launcher and the kit that builds the
# engine from the player's own disc (kit/madeline_kit.py), for Windows 10/11
# x64. Made on Linux (or macOS): zig cross-compiles the host and the launcher
# against the official SDL2 builds, so no Windows machine is needed to package
# it; CI then runs the kit's selftest on one.
#   scripts/package/windows.sh [OUT]   -> OUT/Yellow_Hat_Across_Europe-windows-x86_64.zip
#
# In the zip, one folder, "Yellow Hat Across Europe":
#   Yellow Hat Across Europe.exe    the launcher
#   kit\                            the kit (scripts/package/common.sh); host\bin has SDL's DLLs,
#                                   which the kit copies beside the engine it builds
set -eu
# shellcheck source=scripts/package/common.sh
. "$(dirname "$0")/common.sh"
OUT=$(mkdir -p "${1:-$ROOT/build/release}" && cd "${1:-$ROOT/build/release}" && pwd)
NAME="Yellow Hat Across Europe"
STAGE=$ROOT/build/win-release
APP=$STAGE/$NAME
KIT=$APP/kit
M=x86_64-w64-mingw32
cd "$ROOT"

ZIG=$(host_zig)
SDLW=$CACHE/win-sdl
rm -rf "$SDLW" && mkdir -p "$SDLW"
tar -xzf "$(fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-devel-$SDL2_VERSION-mingw.tar.gz")" -C "$SDLW"
tar -xzf "$(fetch "https://github.com/libsdl-org/SDL_ttf/releases/download/release-$SDL2_TTF_VERSION/SDL2_ttf-devel-$SDL2_TTF_VERSION-mingw.tar.gz")" -C "$SDLW"
SDL=$SDLW/SDL2-$SDL2_VERSION/$M
TTF=$SDLW/SDL2_ttf-$SDL2_TTF_VERSION/$M

# The host, through the Makefile's Windows branch (as on MSYS2), compiled by
# zig into build/win. Linking the stub engine there proves it links.
make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" STUB=1 B=build/win UNAME=MINGW64_NT CC="$ZIG cc -target x86_64-windows-gnu" \
    SDL="-I$SDL/include/SDL2 -I$SDL/include -I$TTF/include/SDL2 -I$TTF/include" \
    LIBS="$TTF/lib/libSDL2_ttf.dll.a $SDL/lib/libSDL2.dll.a -lwinmm -lshell32"

rm -rf "$STAGE" && mkdir -p "$APP"
"$ZIG" cc -target x86_64-windows-gnu -O2 -Wall -municode -Wl,--subsystem,windows -o "$APP/$NAME.exe" \
    launcher/win/launcher.c launcher/win/launcher.rc -lcomctl32 -lcomdlg32 -lshell32 -lole32 -lgdi32 -lmsimg32
rm -f "$APP/$NAME.pdb"

host_objects "$KIT" build/win/obj
mkdir -p "$KIT/host/lib" "$KIT/host/bin"
cp "$SDL/lib/libSDL2.dll.a" "$TTF/lib/libSDL2_ttf.dll.a" "$KIT/host/lib/"
cp "$SDL/bin/SDL2.dll" "$TTF/bin/SDL2_ttf.dll" "$KIT/host/bin/"
kit_common "$KIT" x86_64-windows x86_64-pc-windows-msvc win_amd64
# shellcheck disable=SC2046  # one argument per object
COPY="@/host/bin/SDL2.dll @/host/bin/SDL2_ttf.dll" write_kit_json "$KIT" x86_64-windows-gnu \
    $(host_link_args "$KIT") @/host/lib/libSDL2_ttf.dll.a @/host/lib/libSDL2.dll.a -lwinmm -lshell32

ZIP="$OUT/Yellow_Hat_Across_Europe-windows-x86_64.zip"
rm -f "$ZIP"
(cd "$STAGE" && python3 -c 'import shutil, sys; shutil.make_archive(sys.argv[1][:-4], "zip", ".", sys.argv[2])' "$ZIP" "$NAME")
echo "built $ZIP"

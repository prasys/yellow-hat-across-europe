#!/bin/sh
# The macOS release: the app (the SwiftUI launcher) with the kit inside it that
# builds the engine from the player's own disc (kit/madeline_kit.py), in a dmg.
# Built on a Mac (CI's macos runner) for that Mac's architecture; macOS 13+
# (the launcher's SwiftUI). Needs Xcode's command-line tools, Python 3, curl.
#   scripts/package/macos.sh [OUT]   -> OUT/Yellow_Hat_Across_Europe-macos-<arch>.dmg
#
# Inside the app, besides the launcher:
#   Contents/Resources/kit/                 the kit (scripts/package/common.sh)
#   Contents/Resources/kit/host/Frameworks  SDL2 and SDL2_ttf (the official builds), which
#                                           the kit copies beside the engine it builds
#   Contents/Resources/kit/host/sys         link stubs for the system libraries the host uses
#
# The app is signed ad hoc only (no Apple Developer ID here): the first time,
# macOS asks to have it opened from System Settings > Privacy & Security.
set -eu
# shellcheck source=scripts/package/common.sh
. "$(dirname "$0")/common.sh"
OUT=$(mkdir -p "${1:-$ROOT/build/release}" && cd "${1:-$ROOT/build/release}" && pwd)
ARCH=$(uname -m)
case $ARCH in arm64) ZARCH=aarch64 ;; x86_64) ZARCH=x86_64 ;; *) echo "no release for $ARCH" >&2; exit 1 ;; esac
APPNAME="The Little Girl in the Yellow Hat Across Europe"
STAGE=$ROOT/build/mac-release
APP=$STAGE/$APPNAME.app
KIT=$APP/Contents/Resources/kit
MIN=11.0
export MACOSX_DEPLOYMENT_TARGET=$MIN
cd "$ROOT"

# SDL2 and SDL2_ttf from their official disk images: frameworks that run on
# every macOS this release does, where Homebrew's are built for the runner's.
FW=$CACHE/mac-sdl
rm -rf "$FW" && mkdir -p "$FW/include/SDL2"
for spec in "SDL:SDL2:$SDL2_VERSION" "SDL_ttf:SDL2_ttf:$SDL2_TTF_VERSION"; do
    repo=${spec%%:*} rest=${spec#*:}; name=${rest%%:*} ver=${rest#*:}
    dmg=$(fetch "https://github.com/libsdl-org/$repo/releases/download/release-$ver/$name-$ver.dmg")
    mnt=$(mktemp -d)
    hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$dmg" >/dev/null
    src=$(find "$mnt" -type d -name "$name.framework" | grep -v -e ios -e tvos | head -1)
    [ -n "$src" ] || { hdiutil detach "$mnt" >/dev/null; echo "no $name.framework in $dmg" >&2; exit 1; }
    cp -R "$src" "$FW/"
    hdiutil detach "$mnt" >/dev/null
    # One include folder for both: the host includes <SDL2/SDL_ttf.h> beside <SDL2/SDL.h>.
    for h in "$FW/$name.framework/Headers/"*.h; do ln -sf "$h" "$FW/include/SDL2/"; done
done

# The host, through the Makefile's macOS branch, into build/mac. Linking the
# stub engine there (and starting it) proves the frameworks are right.
make -j"$(sysctl -n hw.ncpu)" STUB=1 B=build/mac OPT="-O2 -mmacosx-version-min=$MIN" GENOPT="-O1 -mmacosx-version-min=$MIN" \
    SDL="-I$FW/include -I$FW/include/SDL2" \
    LIBS="-F$FW -framework SDL2 -framework SDL2_ttf -framework AudioToolbox -framework CoreFoundation -framework AppKit -framework PDFKit -Wl,-rpath,$FW"
scripts/smoke.sh build/mac/madeline

# The app: the launcher as `make app` makes it, without an engine.
make build/launcher build/AppIcon.icns
rm -rf "$STAGE" && mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp launcher/Info.plist "$APP/Contents/Info.plist"
cp build/AppIcon.icns "$APP/Contents/Resources/"
cp build/launcher "$APP/Contents/MacOS/"

host_objects "$KIT" build/mac/obj
mkdir -p "$KIT/host/Frameworks"
cp -R "$FW/SDL2.framework" "$FW/SDL2_ttf.framework" "$KIT/host/Frameworks/"
# zig links for macOS without Apple's SDK, which holds the link stubs (.tbd)
# of the system's libraries. The stub engine linked above by Apple's linker
# names the ones the host needs (otool -L); for each, a stub is written that
# lists only the symbols and Objective-C classes the host takes from it.
SDK=$(xcrun --show-sdk-path)
nm -u "$KIT"/host/obj/*.o | awk 'NF == 1 || $1 == "U" { print $NF }' | sort -u > "$STAGE/used"
# shellcheck disable=SC2016  # a literal $ in the symbol name
sed -n 's/^_OBJC_CLASS_\$_//p' "$STAGE/used" | sort -u > "$STAGE/classes"
SYSLINK=""
for lib in $(otool -L build/mac/madeline | awk 'NR > 1 { print $1 }'); do
    case $lib in
        @rpath/*|/usr/lib/libSystem.B.dylib) continue ;;      # SDL is copied beside the engine; zig has libSystem
        /System/Library/Frameworks/*.framework/*)
            name=${lib#/System/Library/Frameworks/}; name=${name%%.framework/*}
            tbd=$SDK/System/Library/Frameworks/$name.framework/$name.tbd
            out=$KIT/host/sys/$name.framework/$name.tbd
            SYSLINK="$SYSLINK -framework $name" ;;
        /usr/lib/lib*.dylib)
            name=${lib#/usr/lib/lib}; name=${name%%.*}
            tbd=$SDK/usr/lib/lib$name.tbd; [ -f "$tbd" ] || tbd=$SDK/usr/lib/lib$name.A.tbd
            out=$KIT/host/sys/lib$name.tbd
            SYSLINK="$SYSLINK -l$name" ;;
        *) echo "the host links $lib, which this script does not know how to stub" >&2; exit 1 ;;
    esac
    [ -f "$tbd" ] || { echo "no link stub for $lib in $SDK" >&2; exit 1; }
    mkdir -p "$(dirname "$out")"
    grep -oE '_[A-Za-z0-9_]+' "$tbd" | sort -u | comm -12 - "$STAGE/used" > "$STAGE/syms"
    grep -oE '[A-Za-z0-9_]+' "$tbd" | sort -u | comm -12 - "$STAGE/classes" > "$STAGE/objc"
    {
        printf -- "--- !tapi-tbd\ntbd-version: 4\ntargets: [ x86_64-macos, arm64-macos ]\ninstall-name: '%s'\n" "$lib"
        printf "exports:\n  - targets: [ x86_64-macos, arm64-macos ]\n"
        [ -s "$STAGE/syms" ] && printf "    symbols: [ %s ]\n" "$(paste -sd, - < "$STAGE/syms" | sed 's/,/, /g')"
        [ -s "$STAGE/objc" ] && printf "    objc-classes: [ %s ]\n" "$(paste -sd, - < "$STAGE/objc" | sed 's/,/, /g')"
        printf "...\n"
    } > "$out"
    echo "link stub: $lib ($(wc -l < "$STAGE/syms" | tr -d ' ') symbols, $(wc -l < "$STAGE/objc" | tr -d ' ') classes)"
done
kit_common "$KIT" "$ZARCH-macos" "$ZARCH-apple-darwin" \
    macosx_11_0_arm64 macosx_12_0_arm64 macosx_13_0_arm64 macosx_10_9_universal2 macosx_11_0_universal2 macosx_10_9_x86_64 macosx_11_0_x86_64
# shellcheck disable=SC2046,SC2086  # one argument per object; SYSLINK is a list of options
COPY="@/host/Frameworks/SDL2.framework @/host/Frameworks/SDL2_ttf.framework" write_kit_json "$KIT" "$ZARCH-macos.$MIN" \
    $(host_link_args "$KIT") -F@/host/Frameworks -framework SDL2 -framework SDL2_ttf \
    -F@/host/sys -L@/host/sys $SYSLINK -Wl,-rpath,@executable_path

# Ad hoc: what an unsigned app needs to run at all on Apple silicon. The kit's
# programs (python, zig) carry their own signatures.
codesign --force --sign - "$APP/Contents/MacOS/launcher"
codesign --force --sign - "$APP"

DMG="$OUT/Yellow_Hat_Across_Europe-macos-$ARCH.dmg"
rm -f "$DMG"
ln -s /Applications "$STAGE/Applications"
rm -f "$STAGE/used" "$STAGE/classes" "$STAGE/syms" "$STAGE/objc"
hdiutil create -quiet -volname "Yellow Hat Across Europe" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
echo "built $DMG"

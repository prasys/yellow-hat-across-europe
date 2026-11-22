#!/bin/sh
# The Linux release: one AppImage holding the launcher (GTK 4, bundled) and the
# kit that builds the engine from the player's own disc (kit/madeline_kit.py).
#   scripts/package/linux.sh [OUT]          -> OUT/Yellow_Hat_Across_Europe-<arch>.AppImage
# Built on Ubuntu 24.04 (CI's runner, or an ubuntu:24.04 container), x86_64 or
# aarch64: the launcher needs GTK 4.12, and the AppImage runs on systems with
# that glibc (2.39) or newer. Needs the packages the CI release job installs.
#
# Inside, besides GTK and the libraries linuxdeploy collects:
#   usr/bin/yellow-hat-across-europe              the launcher
#   usr/lib/yellow-hat-across-europe/kit/         the kit (scripts/package/common.sh)
#   apprun-hooks/madeline.sh                      where the engine finds its libraries
set -eu
# shellcheck source=scripts/package/common.sh
. "$(dirname "$0")/common.sh"
OUT=$(mkdir -p "${1:-$ROOT/build/release}" && cd "${1:-$ROOT/build/release}" && pwd)
ARCH=$(uname -m)
NAME=yellow-hat-across-europe
APP_ID=io.github.prasys.YellowHatAcrossEurope
APPDIR=$ROOT/build/AppDir
KIT=$APPDIR/usr/lib/$NAME/kit
MULTIARCH=$(gcc -dumpmachine)
cd "$ROOT"

# The host (src/ and win32hle), compiled here by the system's gcc; zig links it
# to the lift on the player's machine, for this glibc.
make -j"$(nproc)" STUB=1
make build/launcher-linux
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/scalable/apps"
host_objects "$KIT" build/stub/obj
case $ARCH in
    x86_64)  WHEELS="manylinux2014_x86_64 manylinux_2_17_x86_64 manylinux1_x86_64" ;;
    aarch64) WHEELS="manylinux2014_aarch64 manylinux_2_17_aarch64" ;;
    *) echo "no release for $ARCH" >&2; exit 1 ;;
esac
# shellcheck disable=SC2086  # a list of tags
kit_common "$KIT" "$ARCH-linux" "$ARCH-unknown-linux-gnu" $WHEELS
GLIBC=$(ldd --version | head -1 | awk '{ print $NF }')
# The engine's libraries are linked by file, from usr/lib where linuxdeploy
# puts them (the kit is two folders below it). Their own dependencies are
# found when the engine starts, so the linker is not asked to resolve them.
ENGINE_LIBS="libSDL2-2.0.so.0 libSDL2_ttf-2.0.so.0 libfluidsynth.so.3"
set --
for l in $ENGINE_LIBS; do set -- "$@" "@/../../$l"; done
# shellcheck disable=SC2046  # one argument per object
LIBPATH=@/../.. write_kit_json "$KIT" "$ARCH-linux-gnu.$GLIBC" $(host_link_args "$KIT") "$@" -lpthread -lm -Wl,--allow-shlib-undefined

cp build/launcher-linux "$APPDIR/usr/bin/$NAME"
cp launcher/icon.svg "$APPDIR/usr/share/icons/hicolor/scalable/apps/$APP_ID.svg"
cat > "$APPDIR/usr/share/applications/$APP_ID.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Yellow Hat Across Europe
GenericName=Madeline: European Adventures
Comment=Madeline: European Adventures (1996), from your own disc
Exec=$NAME %f
Icon=$APP_ID
Terminal=false
Categories=Game;KidsGame;
StartupWMClass=$APP_ID
EOF

LD=$(fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-$ARCH.AppImage")
GTK=$CACHE/gtk-$LINUXDEPLOY_GTK_COMMIT/linuxdeploy-plugin-gtk.sh
[ -s "$GTK" ] || { mkdir -p "${GTK%/*}" && curl -fsSL --retry 3 -o "$GTK" \
    "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/$LINUXDEPLOY_GTK_COMMIT/linuxdeploy-plugin-gtk.sh"; }
chmod +x "$LD" "$GTK"
PLUGINS=$CACHE/plugins && mkdir -p "$PLUGINS" && ln -sf "$GTK" "$PLUGINS/linuxdeploy-plugin-gtk.sh"
export PATH="$PLUGINS:$PATH" DEPLOY_GTK_VERSION=4 APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
set -- --appdir "$APPDIR" -e "$APPDIR/usr/bin/$NAME" -d "$APPDIR/usr/share/applications/$APP_ID.desktop" \
    -i "$APPDIR/usr/share/icons/hicolor/scalable/apps/$APP_ID.svg"
for l in $ENGINE_LIBS; do set -- "$@" -l "/usr/lib/$MULTIARCH/$l"; done
"$LD" "$@" --plugin gtk

# The engine is built outside the AppImage (in the data folder) and started by
# the launcher, which gives it this AppImage's libraries (SDL and the rest) on
# LD_LIBRARY_PATH: that process only. Exported to everything, Ubuntu's
# libraries would replace the system's in whatever the launcher opens (a file
# manager; even /bin/sh breaks on a newer distribution's readline).
# The kit can also be run directly: ./Yellow_Hat_Across_Europe.AppImage --kit selftest DIR
# (this_dir is set by linuxdeploy's AppRun: an APPDIR inherited from another
# AppImage that started this one would point into that one.)
cat > "$APPDIR/apprun-hooks/madeline.sh" <<'EOF'
here=${this_dir:-$APPDIR}
export MADELINE_KIT="$here/usr/lib/yellow-hat-across-europe/kit"
export MADELINE_ENGINE_LIBS="$here/usr/lib"
if [ "${1:-}" = --kit ]; then
    shift
    exec "$MADELINE_KIT/python/bin/python3" -I -B "$MADELINE_KIT/madeline_kit.py" "$@"
fi
EOF
export LDAI_OUTPUT="$OUT/Yellow_Hat_Across_Europe-$ARCH.AppImage" OUTPUT="$OUT/Yellow_Hat_Across_Europe-$ARCH.AppImage"
"$LD" --appdir "$APPDIR" --output appimage
echo "built $OUTPUT"

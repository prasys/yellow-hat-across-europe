# Shared by scripts/package/{linux,windows,macos}.sh: the pinned versions of
# what a release carries, and the parts of the kit (kit/madeline_kit.py) that
# are the same on every system. Sourced, not run.
#
# A release holds no game: the kit builds the engine on the player's machine
# from their own disc. Everything fetched here is the tools for that.
# shellcheck shell=sh

ZIG_VERSION=0.16.0
PY_VERSION=3.12.15
PY_BUILD=20261021            # astral-sh/python-build-standalone release
SDL2_VERSION=2.32.10         # Windows and macOS: the official builds
SDL2_TTF_VERSION=2.24.0
CAPSTONE_VERSION=5.0.9       # the lifter's disassembler: a new one could change the lift
PYCDLIB_VERSION=1.22.0
LINUXDEPLOY_TAG=1-alpha-20251107-1
LINUXDEPLOY_GTK_COMMIT=7a3fbc31a9e5075073ff8790f26effbac5f84453
export ZIG_VERSION PY_VERSION PY_BUILD SDL2_VERSION SDL2_TTF_VERSION CAPSTONE_VERSION PYCDLIB_VERSION LINUXDEPLOY_TAG LINUXDEPLOY_GTK_COMMIT

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CACHE=${PACKAGE_CACHE:-$ROOT/build/package-cache}
mkdir -p "$CACHE"

# fetch URL: download once into $CACHE, print the local path.
fetch() {
    f="$CACHE/$(basename "$1")"
    if [ ! -s "$f" ]; then
        curl -fsSL --retry 3 -o "$f.part" "$1" || { echo "could not download $1" >&2; return 1; }
        mv "$f.part" "$f"
    fi
    printf '%s\n' "$f"
}

# A zig for the machine doing the packaging (to cross-compile the Windows
# host and launcher), as opposed to the one put in a kit. Prints its path.
host_zig() {
    hz="$(uname -m)-$(uname -s | tr '[:upper:]' '[:lower:]' | sed 's/darwin/macos/')"
    if [ ! -x "$CACHE/zig-$hz-$ZIG_VERSION/zig" ]; then
        tar -xJf "$(fetch "https://ziglang.org/download/$ZIG_VERSION/zig-$hz-$ZIG_VERSION.tar.xz")" -C "$CACHE"
    fi
    printf '%s\n' "$CACHE/zig-$hz-$ZIG_VERSION/zig"
}

# What the engine's stamp calls this build: an app update rebuilds the engine.
kit_version() {
    git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo dev
}

# kit_common KIT ZIG_SYSTEM PY_SYSTEM PIP_PLATFORM...
#   ZIG_SYSTEM    zig's name for the system: x86_64-linux, aarch64-macos, x86_64-windows...
#   PY_SYSTEM     python-build-standalone's: x86_64-unknown-linux-gnu, ...
#   PIP_PLATFORM  pip --platform tags the wheels may have (several allowed)
# Lays out the kit's tool, the recompiler, a Python with the lifter's two
# packages, and zig. kit.json and host/ are the caller's.
kit_common() {
    kit=$1 zs=$2 ps=$3; shift 3
    mkdir -p "$kit"
    cp "$ROOT/kit/madeline_kit.py" "$kit/"
    # The recompiler's tools only: its runtime is in host/, already compiled.
    rm -rf "$kit/pcrecomp" && mkdir -p "$kit/pcrecomp"
    (cd "$ROOT/third_party/pcrecomp" && tar --exclude=__pycache__ -cf - tools) | tar -xf - -C "$kit/pcrecomp"

    py=$(fetch "https://github.com/astral-sh/python-build-standalone/releases/download/$PY_BUILD/cpython-$PY_VERSION+$PY_BUILD-$ps-install_only_stripped.tar.gz")
    rm -rf "$kit/python" && tar -xzf "$py" -C "$kit"
    # What the kit never runs: Tk, IDLE, the test suite, pip's installer.
    for d in "$kit"/python/tcl "$kit"/python/lib/tcl* "$kit"/python/lib/tk* "$kit"/python/lib/itcl* "$kit"/python/lib/thread* \
             "$kit"/python/Lib "$kit"/python/lib/python3.*; do
        [ -d "$d" ] || continue
        case $d in */Lib|*/python3.*) rm -rf "$d/idlelib" "$d/tkinter" "$d/turtledemo" "$d/test" "$d/ensurepip" "$d/lib2to3" ;; *) rm -rf "$d" ;; esac
    done
    # capstone has a compiled part: the wheel for the target system, whatever
    # system this runs on. pycdlib is pure Python.
    plat=""; for p in "$@"; do plat="$plat --platform $p"; done
    rm -rf "$kit/site"
    # shellcheck disable=SC2086  # $plat is a list of options
    PIP_BREAK_SYSTEM_PACKAGES=1 python3 -m pip install -q --disable-pip-version-check --target "$kit/site" --only-binary=:all: \
        --python-version "${PY_VERSION%.*}" --implementation cp $plat "capstone==$CAPSTONE_VERSION" "pycdlib==$PYCDLIB_VERSION"

    case $zs in *windows*) zext=zip ;; *) zext=tar.xz ;; esac
    z=$(fetch "https://ziglang.org/download/$ZIG_VERSION/zig-$zs-$ZIG_VERSION.$zext")
    rm -rf "$kit/zig" "$kit/zig-$zs-$ZIG_VERSION"
    case $zext in zip) (cd "$kit" && unzip -q "$z") ;; *) tar -xJf "$z" -C "$kit" ;; esac
    mv "$kit/zig-$zs-$ZIG_VERSION" "$kit/zig"
    # zig carries the C library headers of every system it can target; this kit
    # compiles for one, so the others' are left out (the selftest proves what
    # remains is enough).
    case $zs in
        *linux*)   drop="musl mingw wasi darwin freebsd netbsd openbsd"
                   keep="generic-glibc any-linux-any x86-linux-any x86-linux-gnu aarch64-linux-any aarch64-linux-gnu" ;;
        *windows*) drop="musl glibc wasi darwin freebsd netbsd openbsd"
                   keep="any-windows-any" ;;
        *macos*)   drop="musl glibc mingw wasi freebsd netbsd openbsd"
                   keep="any-darwin-any" ;;
    esac
    for d in $drop; do rm -rf "$kit/zig/lib/libc/$d"; done
    for d in "$kit"/zig/lib/libc/include/*; do
        case " $keep " in *" ${d##*/} "*) ;; *) rm -rf "$d" ;; esac
    done
    rm -rf "$kit/zig/doc" "$kit/zig/lib/libcxx" "$kit/zig/lib/libcxxabi" "$kit/zig/lib/libtsan" "$kit/zig/lib/docs"
}

# write_kit_json KIT TARGET LINK_ARG...: kit.json for madeline_kit.py. Paths
# inside the kit are written @/path. COPY (space separated, @/ paths) names
# files the engine loads from beside itself; LIBPATH, a folder its libraries
# are loaded from instead (Linux: the AppImage's, given to the engine alone).
write_kit_json() {
    kit=$1 target=$2; shift 2
    python3 - "$kit/kit.json" "$(kit_version)" "$target" "${COPY:-}" "${LIBPATH:-}" "$@" <<'EOF'
import json, sys
out, version, target, copy, libpath, *link = sys.argv[1:]
with open(out, "w") as f:
    json.dump({
        "version": version,
        "target": target,
        "zig": "@/zig/zig.exe" if "windows" in target else "@/zig/zig",
        "pcrecomp": "@/pcrecomp",
        "site": "@/site",
        "include": ["@/host/include"],
        "defines": ["-DRECOMP_MEM_BASE=0x100000000000ull"],
        "link": link,
        "copy": copy.split(),
        "libpath": libpath,
    }, f, indent=2)
EOF
}

# host_objects KIT OBJDIR: the host's objects, from `make STUB=1` (nothing of
# them depends on the lift), and the one header the lift includes. Objects,
# not an archive: an archive would drop any whose only use is a constructor.
host_objects() {
    kit=$1 obj=$2
    rm -rf "$kit/host" && mkdir -p "$kit/host/obj" "$kit/host/include"
    cp "$obj"/hle/*.o "$obj"/src/*.o "$kit/host/obj/"
    for d in win mac; do                      # src/win (Windows), src/mac (macOS)
        if [ -d "$obj/src/$d" ]; then cp "$obj/src/$d"/*.o "$kit/host/obj/"; fi
    done
    cp "$ROOT/third_party/pcrecomp/runtime/recomp32/recomp_types.h" "$kit/host/include/"
}

# The host objects as kit.json link arguments.
host_link_args() {
    for o in "$1"/host/obj/*.o; do printf '@/host/obj/%s\n' "${o##*/}"; done
}

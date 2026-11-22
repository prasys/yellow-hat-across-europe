"""The engine builder a release carries: what `make` does in a checkout, done
on the player's machine with nothing installed.

The engine is the game's own code, lifted from the player's MADEURO.EXE, so a
release cannot contain it. It carries everything else instead, beside this
file (the "kit", laid out by scripts/package/*.sh and described by kit.json):

    kit.json      what to compile with and link against, written at packaging
    python/       a portable CPython that runs this file (as `python -I -B`: the
                  kit is read-only, in an AppImage or a signed app)
    site/         capstone (the lifter's disassembler) and pycdlib (ISO reader)
    pcrecomp/     the recompiler's tools/ (third_party/pcrecomp)
    zig/          zig, used as a C compiler and linker for this one system
    host/         the host (src/ + pcrecomp's win32hle) already compiled, and
                  the libraries it is linked with

    madeline_kit.py extract [--replace] IMAGE DEST   unpack a disc image (.iso, or a .zip holding one)
    madeline_kit.py build DISC ENGINE    lift DISC/MADEURO.EXE, compile, link ENGINE/madeline
    madeline_kit.py check DISC ENGINE    0: ENGINE was built for this disc by this kit; 3: build it
    madeline_kit.py selftest ENGINE      build from an empty lift and start it (CI, troubleshooting)

Exit codes follow the engine's: 65 not the game (no MADEURO.EXE), 66 no
such file or folder, 3 the engine needs building, 1 anything else.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
from concurrent.futures import ThreadPoolExecutor

KIT = os.path.dirname(os.path.abspath(__file__))
WINDOWS = os.name == "nt"
EXE_NAME = "madeline.exe" if WINDOWS else "madeline"
STAMP = "kit-stamp"
# Windows: the launcher has no console, and every console program started
# without one would open its own window for a moment (zig, many times over).
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
UNPACKED_MARK = ".unpacked-by-madeline-kit"
# Functions per lifted source file: about a dozen files for this game, so the
# compile can use every core instead of one 9 MB file on one.
SPLIT = 80


class KitError(Exception):
    def __init__(self, message, code=1):
        super().__init__(message)
        self.code = code


def say(*parts):
    print(*parts, flush=True)


def load_kit():
    path = os.path.join(KIT, "kit.json")
    if not os.path.isfile(path):
        raise KitError(f"no kit.json beside {__file__}: this is a release kit's tool (in a checkout, use make)")
    with open(path, encoding="utf-8") as f:
        kit = json.load(f)
    sys.path[:0] = [at(kit.get("site", "@/site"))]
    return kit


def at(arg):
    """kit.json names files inside the kit as @/path."""
    return arg.replace("@/", KIT + os.sep)


# ---- which MADEURO.EXE ------------------------------------------------------

def _crc_table():
    table = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) if c & 0x80000000 else (c << 1)
        table.append(c & 0xFFFFFFFF)
    return table


_CRC = _crc_table()


def posix_cksum(path):
    """The CRC of POSIX `cksum`, which the Makefile and src/main.c use to tie an
    engine to one MADEURO.EXE: CRC-32 (0x04C11DB7, unreflected) over the bytes
    and then the length, complemented."""
    crc, size = 0, 0
    with open(path, "rb") as f:
        while chunk := f.read(1 << 16):
            size += len(chunk)
            for b in chunk:
                crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC[(crc >> 24) ^ b]
    n = size
    while n:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC[(crc >> 24) ^ (n & 0xFF)]
        n >>= 8
    return (~crc) & 0xFFFFFFFF, size


def find_exe(disc):
    """DISC/MADEURO.EXE, in whatever case the disc was unpacked with."""
    if not os.path.isdir(disc):
        raise KitError(f"no folder at {disc}", 66)
    for name in os.listdir(disc):
        if name.upper() == "MADEURO.EXE":
            return os.path.join(disc, name)
    raise KitError(f"no MADEURO.EXE in {disc}: is it Madeline: European Adventures?", 66)


def stamp_line(kit, exe):
    crc, size = posix_cksum(exe)
    return f"{kit['version']} {kit['target']} {crc} {size}"


# ---- extract ----------------------------------------------------------------

def _iso_from_zip(image, workdir):
    """The .iso inside a .zip (as the disc is usually downloaded), copied out:
    pycdlib needs to seek, and a compressed member cannot."""
    with zipfile.ZipFile(image) as z:
        isos = [m for m in z.infolist() if m.filename.lower().endswith(".iso") and not m.is_dir()]
        if not isos:
            raise KitError(f"{image} holds no .iso")
        member = max(isos, key=lambda m: m.file_size)
        say(f"taking {member.filename} out of the zip")
        out = os.path.join(workdir, "disc.iso")
        with z.open(member) as src, open(out, "wb") as dst:
            shutil.copyfileobj(src, dst, 1 << 20)
        return out


def _iso_names(iso):
    """Which names to unpack under: Rock Ridge or Joliet when the disc has them
    (long, mixed case), else ISO 9660's, which end in ';1'."""
    if iso.has_rock_ridge():
        return "rr_path"
    if iso.has_joliet():
        return "joliet_path"
    return "iso_path"


def _clean(name):
    """An ISO 9660 name as a file name: no ';1' version, no '.' left by a name
    without an extension ('README.;1'). A name that could reach outside the
    folder being filled ('..', a separator, a drive) is refused: the image is
    whatever the player downloaded."""
    name = name.split(";", 1)[0].removesuffix(".")
    if name in ("", ".", "..") or any(c in name for c in '/\\:\0'):
        raise KitError(f"the disc image has a file name that is not allowed: {name!r}")
    return name


def _unpack(iso, kind, into):
    files = [(path, name) for path, _dirs, names in iso.walk(**{kind: "/"}) for name in names]
    say(f"unpacking {len(files)} files")
    root = os.path.realpath(into)
    for i, (path, name) in enumerate(files, 1):
        rel = [_clean(p) for p in path.strip("/").split("/") if p] + [_clean(name)]
        out = os.path.join(into, *rel)
        if os.path.commonpath([root, os.path.realpath(out)]) != root:
            raise KitError(f"the disc image has a file outside its own folders: {path}/{name}")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        iso.get_file_from_iso(out, **{kind: path.rstrip("/") + "/" + name})
        if i % 200 == 0:
            say(f"unpacked {i}/{len(files)}")


def extract(image, dest, replace=False):
    """Unpack IMAGE into DEST: 65 when it is not the game's disc, 1 when it
    cannot be read. Everything is unpacked beside DEST under a name of its
    own, and only swapped in once it is known to be the game."""
    if not os.path.isfile(image):
        raise KitError(f"no such file: {image}", 66)
    load_kit()       # puts site/ on the path
    import pycdlib   # from site/; imported here so that `check` needs nothing
    dest = os.path.abspath(dest)
    parent = os.path.dirname(dest)
    os.makedirs(parent, exist_ok=True)
    # Replace an earlier unpack only if this tool made it, or the caller says
    # DEST is its own (the launchers' data/disc, which they used to fill with
    # bsdtar): DEST may be a folder the player chose, and nothing of theirs is
    # ever deleted.
    if os.path.exists(dest) and not replace and not os.path.exists(os.path.join(dest, UNPACKED_MARK)):
        raise KitError(f"{dest} exists and was not unpacked by this tool; not replacing it")
    with tempfile.TemporaryDirectory(dir=parent, prefix=".unpacking-") as work:
        partial = os.path.join(work, "disc")
        iso_path = _iso_from_zip(image, work) if zipfile.is_zipfile(image) else image
        iso = pycdlib.PyCdlib()
        try:
            iso.open(iso_path)
        except Exception as e:  # pycdlib raises its own exception types as well as OSError
            raise KitError(f"{image} is not a disc image this can read ({e})") from e
        try:
            _unpack(iso, _iso_names(iso), partial)
        finally:
            iso.close()
        try:
            find_exe(partial)
        except KitError:
            raise KitError("MADEURO.EXE is not on this disc: is it Madeline: European Adventures?", 65) from None
        open(os.path.join(partial, UNPACKED_MARK), "w").close()
        if os.path.exists(dest):
            os.replace(dest, os.path.join(work, "old"))   # removed with the rest of `work`
        os.replace(partial, dest)
    say(f"extracted to {dest}")


# ---- build ------------------------------------------------------------------

def _run(argv, log, env=None):
    proc = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, check=False, creationflags=NO_WINDOW)
    out = proc.stdout.decode(errors="replace")
    if out:
        log.write(out)
        log.flush()
    if proc.returncode != 0:
        tail = "\n".join(out.strip().splitlines()[-15:])
        raise KitError(f"{os.path.basename(argv[0])} failed (exit {proc.returncode}):\n{tail}")


def lift(exe, gen, log):
    say("lifting MADEURO.EXE to C (pcrecomp)")
    shutil.rmtree(gen, ignore_errors=True)
    os.makedirs(gen)
    _run([sys.executable, "-I", "-B", os.path.abspath(__file__), "_lift", exe, gen], log)


def _lift_here(exe, gen):
    """pcrecomp's `python -m tools`, run in this process from the kit's copy."""
    import runpy
    kit = load_kit()
    sys.path.insert(0, at(kit.get("pcrecomp", "@/pcrecomp")))
    sys.argv = ["tools", exe, "--all", "--split", str(SPLIT), "-o", gen]
    runpy.run_module("tools", run_name="__main__", alter_sys=True)


def write_lift_id(gen, exe):
    crc, size = posix_cksum(exe)
    with open(os.path.join(gen, "lift_id.c"), "w") as f:
        f.write(f"#include <stdint.h>\nconst uint32_t madeline_lift_cksum = {crc}u, madeline_lift_size = {size}u;\n")


def zig_env(engine):
    """zig keeps what it builds once (its runtime library) in this cache: inside
    the engine folder, so that nothing is left anywhere else."""
    cache = os.path.join(engine, "zig-cache")
    return dict(os.environ, ZIG_GLOBAL_CACHE_DIR=cache, ZIG_LOCAL_CACHE_DIR=cache)


def compile_and_link(kit, gen, engine, log):
    zig = at(kit["zig"])
    obj = os.path.join(engine, "obj")
    shutil.rmtree(obj, ignore_errors=True)
    os.makedirs(obj)
    env = zig_env(engine)
    sources = sorted(f for f in os.listdir(gen) if f.endswith(".c"))
    flags = ["-target", kit["target"], "-O1", "-w", *kit.get("defines", []),
             *[f"-I{at(i)}" for i in kit.get("include", [])], f"-I{gen}",
             "-include", os.path.join(gen, "recomp_funcs.h")]
    say(f"compiling {len(sources)} files with zig")
    done = [0]

    def one(src):
        out = os.path.join(obj, src[:-2] + ".o")
        _run([zig, "cc", *flags, "-c", os.path.join(gen, src), "-o", out], log, env)
        done[0] += 1
        say(f"compiled {done[0]}/{len(sources)}")
        return out

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
        objects = list(pool.map(one, sources))
    say("linking")
    tmp = os.path.join(engine, "new-" + EXE_NAME)
    _run([zig, "cc", "-target", kit["target"], "-o", tmp, *objects, *[at(a) for a in kit["link"]]], log, env)
    for f in kit.get("copy", []):  # libraries the engine loads from beside itself
        src = at(f)
        dst = os.path.join(engine, os.path.basename(src.rstrip("/\\")))
        if os.path.isdir(src):
            shutil.rmtree(dst, ignore_errors=True)
            shutil.copytree(src, dst, symlinks=True)
        else:
            shutil.copy2(src, dst)
    os.replace(tmp, os.path.join(engine, EXE_NAME))


def build(disc, engine, gen=None):
    kit = load_kit()
    exe = find_exe(disc)
    engine = os.path.abspath(engine)
    os.makedirs(engine, exist_ok=True)
    stamp = os.path.join(engine, STAMP)
    if os.path.exists(stamp):
        os.remove(stamp)  # an engine half rebuilt is not the one the stamp describes
    t0 = time.monotonic()
    own_gen = gen is None
    with open(os.path.join(engine, "build.log"), "w", encoding="utf-8") as log:
        if own_gen:
            gen = os.path.join(engine, "gen")
            lift(exe, gen, log)
        write_lift_id(gen, exe)
        compile_and_link(kit, gen, engine, log)
    # The lift is the game's code: it is not kept once the engine is made.
    shutil.rmtree(os.path.join(engine, "obj"), ignore_errors=True)
    if own_gen:
        shutil.rmtree(gen, ignore_errors=True)
    with open(stamp, "w") as f:
        f.write(stamp_line(kit, exe) + "\n")
    say(f"engine built in {time.monotonic() - t0:.0f} s: {os.path.join(engine, EXE_NAME)}")


def check(disc, engine):
    kit = load_kit()
    exe = find_exe(disc)
    try:
        with open(os.path.join(engine, STAMP)) as f:
            have = f.readline().strip()
    except OSError:
        raise KitError("no engine built yet", 3) from None
    if not os.path.isfile(os.path.join(engine, EXE_NAME)) or have != stamp_line(kit, exe):
        raise KitError("the engine was built for another disc or by another version of this app", 3)
    say("engine built for this disc")


# ---- selftest ---------------------------------------------------------------

EMPTY_LIFT = {   # what `make STUB=1` puts in place of a lift
    "recomp_funcs.h": "#pragma once\n",
    "recomp_dispatch.c": '#include "recomp_types.h"\n'
                         "const recomp_dispatch_entry_t recomp_dispatch_table[] = { { 0, 0 } };\n"
                         "const uint32_t recomp_dispatch_count = 0;\n",
    "recomp_0000.c": "/* no game: an empty lift */\ntypedef int no_lift;\n",
}


def selftest(engine):
    """Everything `build` does but the lift, from an empty one, for a made-up
    MADEURO.EXE; then the engine must start and accept that file (0), refuse
    another (3) and a folder without one (66). Proves the compiler, the host
    and the libraries on this machine without the game."""
    kit = load_kit()
    with tempfile.TemporaryDirectory() as tmp:
        disc, other, empty, gen = (os.path.join(tmp, d) for d in ("disc", "other", "empty", "gen"))
        for d in (disc, other, empty, gen):
            os.makedirs(d)
        for d, content in ((disc, b"MZ selftest"), (other, b"MZ another")):
            with open(os.path.join(d, "MADEURO.EXE"), "wb") as f:
                f.write(content)
        for name, text in EMPTY_LIFT.items():
            with open(os.path.join(gen, name), "w") as f:
                f.write(text)
        build(disc, engine, gen=gen)
        check(disc, engine)
        exe = os.path.join(os.path.abspath(engine), EXE_NAME)
        env = dict(os.environ, MADELINE_SAVE_DIR=os.path.join(tmp, "save"), SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        if kit.get("libpath"):  # where the engine's libraries are, when not beside it (the AppImage)
            env["LD_LIBRARY_PATH"] = at(kit["libpath"])
        for folder, want in ((disc, 0), (other, 3), (empty, 66)):
            got = subprocess.run([exe, "--check", folder], env=env, capture_output=True, check=False, creationflags=NO_WINDOW)
            if got.returncode != want:
                raise KitError(f"selftest: `madeline --check` on {os.path.basename(folder)} gave {got.returncode},"
                               f" expected {want}\n{got.stderr.decode(errors='replace')}")
    say("selftest: ok")


# ---- command line -----------------------------------------------------------

def main(argv):
    # A redirected stdout is in the ANSI code page on Windows: a path it cannot
    # spell must not turn finished work into a failure.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    if argv[:1] == ["_lift"]:
        _lift_here(argv[1], argv[2])
        return 0
    p = argparse.ArgumentParser(prog="madeline_kit", description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("extract", help="unpack a disc image (.iso, or a .zip holding one)")
    s.add_argument("--replace", action="store_true", help="DEST is the caller's own: replace what is there")
    s.add_argument("image")
    s.add_argument("dest")
    s = sub.add_parser("build", help="lift DISC/MADEURO.EXE and build the engine into ENGINE")
    s.add_argument("disc")
    s.add_argument("engine")
    s = sub.add_parser("check", help="exit 0 if ENGINE was built for DISC by this kit, else 3")
    s.add_argument("disc")
    s.add_argument("engine")
    s = sub.add_parser("selftest", help="build from an empty lift into ENGINE and start it")
    s.add_argument("engine")
    a = p.parse_args(argv)
    try:
        if a.cmd == "extract":
            extract(a.image, a.dest, a.replace)
        elif a.cmd == "build":
            build(a.disc, a.engine)
        elif a.cmd == "check":
            check(a.disc, a.engine)
        else:
            selftest(a.engine)
    except KitError as e:
        print(f"madeline_kit: {e}", file=sys.stderr, flush=True)
        return e.code
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

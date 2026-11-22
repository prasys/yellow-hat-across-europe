"""kit/madeline_kit.py without the game: run by tests/run.sh (`make test`).

The disc images here are made on the spot with pycdlib and hold a few bytes
of made-up files, nothing of the game's.
"""
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "kit"))
import madeline_kit as kit  # noqa: E402

import pycdlib  # noqa: E402


def make_iso(path, files):
    """An ISO 9660 image (level 1 names, like the game's disc) holding files: {"DIR/NAME.EXT": bytes}."""
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1)
    made = set()
    for name, data in files.items():
        parts = name.split("/")
        for i in range(1, len(parts)):
            d = "/" + "/".join(parts[:i])
            if d not in made:
                iso.add_directory(d)
                made.add(d)
        iso.add_fp(io.BytesIO(data), len(data), "/" + name + ("" if "." in parts[-1] else ".") + ";1")
    iso.write(path)
    iso.close()


GAME = {"MADEURO.EXE": b"MZ not really the game", "MAD000/MAD000.DAT": b"scene", "README": b"no extension"}


class Cksum(unittest.TestCase):
    """The kit's checksum must be POSIX cksum's: the Makefile ties a checkout's
    engine to a disc with `cksum`, the kit ties a release's with this, and the
    engine (src/main.c) checks either at start."""

    @unittest.skipUnless(shutil.which("cksum"), "no cksum command here")
    def test_matches_cksum(self):
        with tempfile.TemporaryDirectory() as tmp:
            for size in (0, 1, 255, 256, 65535, 65536, 70001, 151552):
                path = os.path.join(tmp, f"f{size}")
                with open(path, "wb") as f:
                    f.write(os.urandom(size))
                with open(path, "rb") as f:
                    out = subprocess.run(["cksum"], stdin=f, capture_output=True, check=True).stdout.split()
                self.assertEqual(kit.posix_cksum(path), (int(out[0]), int(out[1])), f"{size} bytes")

    def test_known_value(self):
        # `printf abc | cksum` on any POSIX system.
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "abc")
            with open(path, "wb") as f:
                f.write(b"abc")
            self.assertEqual(kit.posix_cksum(path), (1219131554, 3))


class KitDir(unittest.TestCase):
    """A made-up kit folder (kit.json only) in place of the release's."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.kit = os.path.join(self.tmp, "kit")
        os.makedirs(self.kit)
        self.write_kit("1.0")
        self.addCleanup(setattr, kit, "KIT", kit.KIT)
        kit.KIT = self.kit

    def write_kit(self, version):
        with open(os.path.join(self.kit, "kit.json"), "w") as f:
            json.dump({"version": version, "target": "x86_64-test"}, f)


class Extract(KitDir):
    def iso(self, files=GAME):
        path = os.path.join(self.tmp, "disc.iso")
        make_iso(path, files)
        return path

    def assert_game(self, dest):
        for name, data in GAME.items():
            with open(os.path.join(dest, *name.split("/")), "rb") as f:
                self.assertEqual(f.read(), data, name)

    def test_iso(self):
        dest = os.path.join(self.tmp, "disc")
        kit.extract(self.iso(), dest)
        self.assert_game(dest)   # ';1' gone, and the '.' of a name without an extension

    def test_iso_in_a_zip(self):
        z = os.path.join(self.tmp, "download.zip")
        with zipfile.ZipFile(z, "w") as f:
            f.write(self.iso(), "Game Files/Madeline.iso")
            f.writestr("ReadMe.txt", "hello")
        dest = os.path.join(self.tmp, "disc")
        kit.extract(z, dest)
        self.assert_game(dest)

    def test_not_the_game(self):
        dest = os.path.join(self.tmp, "disc")
        with self.assertRaises(kit.KitError) as e:
            kit.extract(self.iso({"OTHER.EXE": b"MZ"}), dest)
        self.assertEqual(e.exception.code, 65)
        self.assertFalse(os.path.exists(dest))
        self.assertEqual(sorted(os.listdir(self.tmp)), ["disc.iso", "kit"])   # nothing left behind

    def test_not_an_image(self):
        junk = os.path.join(self.tmp, "junk.iso")
        with open(junk, "wb") as f:
            f.write(b"\0" * 4096)
        with self.assertRaises(kit.KitError) as e:
            kit.extract(junk, os.path.join(self.tmp, "disc"))
        self.assertEqual(e.exception.code, 1)     # unreadable, which is not "not the game" (65)

    def test_refuses_names_that_climb_out(self):
        # Rock Ridge names are taken as they are; "..." would become ".." once
        # the ISO 9660 trailing dot is dropped, and climb out of the folder.
        path = os.path.join(self.tmp, "evil.iso")
        iso = pycdlib.PyCdlib()
        iso.new(interchange_level=1, rock_ridge="1.09")
        iso.add_directory("/A", rr_name="...")
        iso.add_directory("/A/B", rr_name="...")
        iso.add_fp(io.BytesIO(b"x"), 1, "/A/B/ESCAPED.TXT;1", rr_name="escaped.txt")
        iso.add_fp(io.BytesIO(b"MZ"), 2, "/MADEURO.EXE;1", rr_name="MADEURO.EXE")
        iso.write(path)
        iso.close()
        dest = os.path.join(self.tmp, "data", "disc")
        with self.assertRaises(kit.KitError):
            kit.extract(path, dest)
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "escaped.txt")))
        self.assertFalse(os.path.exists(dest))
        self.assertEqual(os.listdir(os.path.join(self.tmp, "data")), [])   # nothing left behind

    def test_no_such_file(self):
        with self.assertRaises(kit.KitError) as e:
            kit.extract(os.path.join(self.tmp, "missing.iso"), os.path.join(self.tmp, "disc"))
        self.assertEqual(e.exception.code, 66)

    def test_leaves_a_folder_it_did_not_make(self):
        dest = os.path.join(self.tmp, "mine")
        os.makedirs(dest)
        with open(os.path.join(dest, "keep.txt"), "w") as f:
            f.write("the player's")
        with self.assertRaises(kit.KitError):
            kit.extract(self.iso(), dest)
        self.assertEqual(os.listdir(dest), ["keep.txt"])

    def test_replace(self):
        dest = os.path.join(self.tmp, "disc")
        os.makedirs(dest)
        with open(os.path.join(dest, "old.txt"), "w") as f:
            f.write("an earlier unpack by other means")
        kit.extract(self.iso(), dest, replace=True)
        self.assert_game(dest)
        self.assertFalse(os.path.exists(os.path.join(dest, "old.txt")))

    def test_unpacks_again_over_its_own(self):
        dest = os.path.join(self.tmp, "disc")
        kit.extract(self.iso({"MADEURO.EXE": b"first", "A.DAT": b"a"}), dest)
        kit.extract(self.iso({"MADEURO.EXE": b"second"}), dest)
        with open(os.path.join(dest, "MADEURO.EXE"), "rb") as f:
            self.assertEqual(f.read(), b"second")
        self.assertFalse(os.path.exists(os.path.join(dest, "A.DAT")))


class Check(KitDir):
    """`check` says whether the engine in a folder was built for this disc by this kit."""

    def setUp(self):
        super().setUp()
        self.disc = os.path.join(self.tmp, "disc")
        os.makedirs(self.disc)
        self.exe = os.path.join(self.disc, "madeuro.exe")   # any case
        with open(self.exe, "wb") as f:
            f.write(b"MZ one")
        self.engine = os.path.join(self.tmp, "engine")
        os.makedirs(self.engine)

    def built(self):
        """What `build` leaves: the engine and its stamp."""
        open(os.path.join(self.engine, kit.EXE_NAME), "w").close()
        with open(os.path.join(self.engine, kit.STAMP), "w") as f:
            f.write(kit.stamp_line(kit.load_kit(), self.exe) + "\n")

    def code(self):
        try:
            kit.check(self.disc, self.engine)
            return 0
        except kit.KitError as e:
            return e.code

    def test_nothing_built(self):
        self.assertEqual(self.code(), 3)

    def test_built_for_this_disc(self):
        self.built()
        self.assertEqual(self.code(), 0)

    def test_another_disc(self):
        self.built()
        with open(self.exe, "wb") as f:
            f.write(b"MZ two")
        self.assertEqual(self.code(), 3)

    def test_another_version_of_the_app(self):
        self.built()
        self.write_kit("1.1")
        self.assertEqual(self.code(), 3)

    def test_engine_missing(self):
        self.built()
        os.remove(os.path.join(self.engine, kit.EXE_NAME))
        self.assertEqual(self.code(), 3)

    def test_no_game_in_the_folder(self):
        os.remove(self.exe)
        self.assertEqual(self.code(), 66)


if __name__ == "__main__":
    unittest.main()

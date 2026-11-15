# 04 - Linux build

## Goal

`make && make run` on a 64-bit Linux desktop (x86-64 and arm64), with music.

## Where it stands (audit of 2026-11-15, nothing built on Linux yet)

Already portable:

- pcrecomp's win32hle is POSIX + SDL2 and documents 64-bit "based" Linux hosts
  (`HLE_BASED=1` in its `build_selftests.sh`).
- Drawing, the pointer, smoothing, sound effects and speech (`gfx.c`,
  `audio.c`) only use SDL through win32hle.
- `src/platform.c` picks the data folder: `$XDG_DATA_HOME/madeline-european-adventures`
  (default `~/.local/share/...`). Written, not yet run on Linux.
- Scripts: `lift.sh` (sha256sum or shasum), `extract_iso.sh` (bsdtar or 7z),
  `playtest.sh` (sips or ffmpeg). The Makefile only adds Apple frameworks on
  Darwin.

## To do

- [ ] **First build.** Packages (Debian/Ubuntu): `build-essential pkg-config
      libsdl2-dev libsdl2-ttf-dev python3-venv libarchive-tools`. Fix what the
      compiler finds. Expect: `-D_GNU_SOURCE`/`_DEFAULT_SOURCE` for `usleep`,
      `strnlen`, `realpath` in `src/`; GNU ld wanting `-lm -lpthread` last.
- [ ] **Guest memory address.** The build passes
      `-DRECOMP_MEM_BASE=0x100000000000ull` (pcrecomp's value for a Mac). Check
      it maps on Linux x86-64 and arm64 (48-bit and 39-bit address spaces
      differ); if not, choose a per-platform value in the Makefile or drop the
      define there and let win32hle pick. Run win32hle's own selftests first
      (`third_party/pcrecomp/runtime/win32hle/build_selftests.sh` with
      `HLE_BASED=1`): if those fail, the problem is upstream, not here.
- [ ] **Headless playtest reaches the main menu, then the station** (file
      trace shows `MAD000/SCRN000.PCX`, then `MAD050`). Case matters now: the
      disc's files are upper-case and the game asks in lower and mixed case.
      win32hle matches names without case; confirm for relative paths too
      (`mad400\palanim.dat` style).
- [ ] **A real window** under X11 and Wayland: picture, pointer, mouse
      position at non-integer scales, F9-F12, close button dialog.
- [ ] **Sound**: SDL audio through PipeWire/PulseAudio; check speech pacing
      (animation is timed by played audio, see `AGENTS.md`).
- [ ] **MIDI** (`src/midi.c`, the `#else` branch is silent): FluidSynth behind
      the same six functions, found with `pkg-config fluidsynth` at build time
      (`-DHAVE_FLUIDSYNTH`), a system SoundFont
      (`/usr/share/sounds/sf2/FluidR3_GM.sf2` and friends, or `soundfont=` in
      `madeline.ini`), silent with a one-line notice when neither is there.
      `madeline_midi_finished` must turn true at the end of the file: the game
      restarts its music on that.
- [ ] **Launcher.** The SwiftUI app is macOS-only. On Linux the game runs
      from the command line and `madeline.ini`; decide whether a small
      launcher is wanted (GTK, or a terminal menu) before building one.
- [ ] **Desktop entry and icon** (`.desktop`, the PNG from
      `launcher/make_icon.swift` checked in as an asset or redrawn in a
      portable script).
- [ ] **CI** (GitHub Actions, Linux + macOS) that builds the host **without a
      game**: a stub `build/gen` (empty dispatch table, `recomp_funcs.h`,
      `lift_id.h`) so `src/` compiles and links against the pinned pcrecomp. No
      game file may be fetched in CI.
- [ ] README: a Linux quick start. ROADMAP: tick what is done.

## Done when

- `scripts/playtest.sh` reaches the station on Linux x86-64 (and arm64 if a
  machine is to hand), and the macOS build and the same playtest still pass.
- A person has played to the station in a window with speech and music.

## Out of scope

Packaging (AppImage, Flatpak). Touching `third_party/pcrecomp`: if win32hle
needs a fix for Linux, describe it under "Upstream" in the roadmap and work
around it in `src/`.

## A way to do this from a Mac

OrbStack or Docker gives a Linux arm64 (and emulated x86-64) userland: build
and run the headless playtest there with the checkout and the extracted disc
mounted. The window, sound and MIDI still need a real Linux desktop.

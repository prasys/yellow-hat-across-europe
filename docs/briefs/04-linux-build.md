# 04 - Linux build

## Goal

`make && make run` on a 64-bit Linux desktop, with music.

## What is known

- pcrecomp's win32hle supports 64-bit "based" hosts on Linux (`HLE_BASED=1` in
  its `build_selftests.sh`); this project already builds that way on macOS
  with `-DRECOMP_MEM_BASE=0x100000000000ull`. Whether that fixed address is
  mappable on Linux is unchecked; without the define, win32hle picks its own.
- macOS-only today: `src/midi.c` (AudioToolbox; the `#else` branch is silent),
  the frameworks in the `Makefile`, the save folder
  (`~/Library/Application Support/...` in `src/main.c`), `scripts/playtest.sh`
  (uses `sips` for PNGs; ffmpeg is the portable choice), `scripts/extract_iso.sh`
  (`bsdtar`; fine if libarchive-tools is installed).
- The pointer, sound effects and speech are already platform-neutral (SDL).

## Build

1. Makefile and scripts that work on both systems without flags.
2. Save folder: `$XDG_DATA_HOME/madeline-european-adventures` (default
   `~/.local/share/...`) on Linux; unchanged on macOS.
3. MIDI on Linux in `src/midi.c` behind the same six functions: FluidSynth with
   a system SoundFont if found at build time, silent otherwise. Document the
   packages.
4. A CI job (GitHub Actions) that builds the host on Linux and macOS **without
   a game**: generate a stub `build/gen` (empty dispatch table and header) so
   `src/` compiles and links against the pinned pcrecomp. No game file may be
   fetched in CI.

## Done when

- A headless `scripts/playtest.sh` reaches the main menu on Linux (file trace
  shows `MAD000/SCRN000.PCX`) and a second run reaches the station.
- The macOS build and the same playtest still pass.
- README has a Linux quick start; ROADMAP is updated.

## Out of scope

Windows. Packaging (AppImage, Flatpak). Touching `third_party/pcrecomp`: if
win32hle needs a fix for Linux, describe it under "Upstream" in the roadmap
and work around it in `src/`.

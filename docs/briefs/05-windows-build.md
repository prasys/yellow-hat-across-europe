# 05 - Windows build

## Goal

The game on Windows 10/11, 64-bit: `madeline.exe`, with the same settings
file, saves and behaviour as the Mac build.

## The ironic part, and why the obvious route is wrong

The game is a Windows program, but its code no longer runs there usefully:
it needs Win32s-era WinG and a 256-colour palette display, neither of which
modern Windows has. So Windows gets the same treatment as the Mac: the lifted
code plus this host's own display (`gfx.c`) and sound (`audio.c`).

pcrecomp has a Windows-only runtime, `native32`, that forwards the game's
imports to the real Win32 API. It is **not** the route: it is 32-bit MSVC,
has no WinG, would bypass `gfx.c`'s palette screen, and would make Windows a
second, different port. Use win32hle on Windows too.

## Where it stood (audit of 2026-11-15, before anything was built for Windows; see "To do" for what has been done since)

- win32hle is written for POSIX: 16 of its files include `pthread.h`,
  `unistd.h`, `sys/mman.h`, `dirent.h` or BSD sockets, and it has no `_WIN32`
  branches. `recomp_types.h` does support MSVC.
- `src/platform.c` already has the Windows data folder
  (`%APPDATA%\MadelineEuropeanAdventures`), unrun.
- `src/` uses `usleep`, `realpath`, `chdir`, `signal(SIGUSR1)`, `<unistd.h>`.
- The lifter is Python + capstone: works on Windows as is.

## Routes, cheapest first

1. **WSL2** (a day at most, after brief 04). The Linux build runs on Windows
   11 through WSLg with window and sound. Zero new code; good for "does it
   work on my PC", not a real Windows program. Document it and move on.
2. **MSYS2 / MinGW-w64, 64-bit** (the real port). MinGW brings winpthreads,
   most of `unistd.h` and `dirent.h`, and MSYS2 packages SDL2 and SDL2_ttf.
   What it lacks, win32hle needs replaced:
   - `mmap` in `guest_mem.c`: the guest's 4 GB at a fixed address, pages
     reserved and committed on demand. On Windows that is `VirtualAlloc`
     (`MEM_RESERVE` at the base, `MEM_COMMIT` per page). `guest_mem.c`
     already has a no-fixed-mmap fallback (for the browser build): read it
     first, it may be enough to start with.
   - BSD sockets in `wsock32.c`: the game uses no network. Leave the file out
     and give `host.c` an empty `win32hle_wsock32` table from `src/`.
   - `statvfs`, `clock_gettime(CLOCK_MONOTONIC)`, `realpath`, `usleep`,
     `mkdir(path, mode)`: small shims.
   - Name clashes: win32hle's shims are named after Win32 functions
     (`k_`/`u_`/`g_` prefixes, so probably fine) and it defines Win32
     constants itself; it must **not** include `<windows.h>` in the same
     file. Put every real Windows call in one separate file.
   Because `third_party/pcrecomp` is not ours to edit, do this as a
   compatibility layer in `src/win/` (a `sys/mman.h` and friends on the
   include path, implemented over the Windows API) and offer the changes
   upstream (roadmap, "Upstream").
3. **MSVC**: only if upstream wants it. More shims (`pthread`, `dirent`,
   `strcasecmp`, GNU attributes used for constructors in `host_lite.c`).

## To do (route 2)

Progress, 2026-11-17 (Windows 11 x64, MSYS2 UCRT64, gcc 16.2, done before
brief 04 after all):

- [x] MSYS2 UCRT64 environment; `pacman` packages listed in the README.
- [x] `src/win/` compatibility headers + `compat_win.c`; Makefile branch for
      `MINGW` (`.exe`, no frameworks). It links without `SDL2main` and
      without `-mwindows` for now: `main()` is the program's own and it
      stays a console program, so what it prints can be read. Make it a
      windowed program when there is a launcher to start it.
- [x] Guest memory on `VirtualAlloc` (a reservation at `RECOMP_MEM_BASE`,
      64 KB pieces committed and decommitted).
- [ ] win32hle's selftests on Windows: not built or run yet.
- [x] Headless playtest to the station, with a save and a load, under MSYS2
      bash (`scripts/playtest.sh` as it is, plus one `cygpath`).
- [x] MIDI: `src/win/midi_win.c`, `mciSendString` on the `sequencer` device.
      Opens the game's `.MID` files and reports their length; nobody has
      listened to it yet.
- [x] Started in a window by a person: picture and sound reported good
      (2026-11-17). F-keys and the close dialog not gone through one by one.

What turned out to matter, beyond the list above:

- **Paths.** win32hle joins host paths with `/` and takes a leading `/` to
  mean absolute. `compat_realpath` hands back `C:/...`; that works for
  everything except `hle_guest_path`, which puts the current directory in
  front of it. `compat_realpath` therefore starts at the last `X:` in what
  it is given (`src/win/compat_win.c`).
- **Binary files.** Windows' `open`/`fopen` translate line ends unless told
  not to; win32hle does not ask. Binary is made the default once at start.
- **Short sleeps.** The game waits in 50-300 microsecond sleeps; Windows'
  `Sleep` counts in milliseconds, 15 at a time by default. `usleep` and
  `nanosleep` use a high-resolution waitable timer.
- **The disc image.** MSYS2's `bsdtar` listed the ISO as empty, without an
  error. `scripts/extract_iso.sh` falls back to letting Windows mount it.
- **Line ends.** Git for Windows checks text out with CRLF by default;
  `.gitattributes` keeps the scripts and the Makefile LF.
- MSYS2's SDL2 is sdl2-compat on SDL3. Headless it behaves; the windowed
  and audible side is part of what a person still has to check.

Still to do:

- [x] `SIGUSR1` screenshots have no Windows equivalent: F9 only there
      (`src/debug.c` leaves the signal out where it does not exist).
- [x] A launcher (2026-11-17): `launcher/win/launcher.c`, a small Win32
      window with the Mac launcher's contents, built by the same gcc and
      linked statically, so it needs no DLL of MSYS2's. Linux still has its
      own launcher question (brief 04).
- [x] Packaging, for the builder's own machine: `make app` makes `build/app`
      with the launcher, `madeline.exe` and the DLLs `ldd` finds, plus
      `SDL3.dll`, which sdl2-compat loads by name. The launcher unpacks a disc
      image (PowerShell's `Mount-DiskImage` and `robocopy`) and runs
      `make engine-into-app` through the MSYS2 recorded in `msys-root.txt`.
- [ ] A version to hand out. Same rule as everywhere: the built program
      contains the lift of the player's own game and is not to be shared; a
      distributable Windows version has to lift on the player's machine too,
      without asking them for MSYS2.
- [ ] Launcher, left: an icon in the program file; disc paths with spaces
      (`make` splits them); per-monitor scaling.
- [ ] CI: add a Windows job to the stub-gen build from brief 04.

## Done when

A person has played to the station on Windows 10 or 11 with speech and
music, the headless playtest passes there, and macOS and Linux still build.

## Depends on

Brief 04: do Linux first. It flushes out everything that is merely
"not macOS", leaving only what is truly Windows.

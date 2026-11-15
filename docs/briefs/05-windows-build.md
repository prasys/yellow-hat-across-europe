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

## Where it stands (audit of 2026-11-15, nothing built for Windows yet)

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

- [ ] MSYS2 UCRT64 environment; `pacman` packages listed in the README.
- [ ] `src/win/` compatibility headers + `compat_win.c`; Makefile branch for
      `MINGW`/`MSYS` (`.exe`, `-lSDL2main`, `-mwindows`, no frameworks).
- [ ] Guest memory on `VirtualAlloc`; win32hle's selftests pass.
- [ ] Headless playtest to the station (needs `scripts/playtest.sh` under
      MSYS2 bash, or a PowerShell twin).
- [ ] MIDI in `src/midi.c`: the real `winmm` (`mciSendString` on the
      `sequencer` device, or `midiStream*`). Mind the name clash: the game's
      own `mciSendCommandA` is answered by `audio.c`; the host's call to the
      real one must live in the Windows-only file.
- [ ] Window, sound, pointer, F-keys, close dialog checked by a person.
- [ ] `SIGUSR1` screenshots have no Windows equivalent: F9 only there.
- [ ] A launcher: the SwiftUI one is macOS-only. Candidates: a small Win32 or
      WinUI settings window, or one cross-platform launcher for Linux and
      Windows together. Decide with brief 04's launcher question.
- [ ] Packaging: a folder with `madeline.exe`, the SDL DLLs and a "choose
      your disc" first run. Same rule as everywhere: the built program
      contains the lift of the player's own game and is not to be shared; a
      distributable Windows version has to lift on the player's machine too.
- [ ] CI: add a Windows job to the stub-gen build from brief 04.

## Done when

A person has played to the station on Windows 10 or 11 with speech and
music, the headless playtest passes there, and macOS and Linux still build.

## Depends on

Brief 04: do Linux first. It flushes out everything that is merely
"not macOS", leaving only what is truly Windows.

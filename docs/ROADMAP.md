# Roadmap: what still needs building

Status as of 2026-11-15: the game boots, plays its movies, music and speech,
takes mouse and keyboard, saves and loads, on macOS (Apple silicon), in a
window. The owner has run it by hand and confirmed it works; scripted tests
have covered start-up through the first scene (the Paris station) only.

Ordered by how much each matters to someone actually playing.

## 1. Prove the whole game, not just the first scene

- [ ] **Walk every location.** There are 45 `MADnnn` folders; only `MAD000`,
      `MAD050`, `MAD130`, `MAD380`, `MAD390` have been entered. Each new screen
      can reach code never run before. What to watch for: `unimplemented
      import`, `no lifted function at 0x…` (a function the recompiler missed,
      reachable only through a pointer: fix by seeding the lift), and a screen
      that stalls (a wait loop the idle hook does not see).
- [ ] **The activities** (the "activities" button: painting with the brush /
      bucket / eraser cursors, postcards, and the rest). Painting is the likely
      user of the drawing paths that are thin today.
- [ ] **A recorded play-through as a regression test**: extend
      `scripts/playtest.sh` scripts into a small set (`tests/*.script`) with
      the expected sequence of files opened, so a change can be checked without
      looking at pictures. Needs "wait until file X is opened" in `debug.c`
      instead of fixed times, which are fragile.
- [ ] **Other releases.** Only one English build of `MADEURO.EXE` is known
      (`docs/MADEURO.sha256`). A different pressing or language (a French
      release, if one exists) will lift, but addresses quoted in the notes may
      differ; `lift.sh` warns on a checksum mismatch.

## 2. Gaps in the host

- [ ] **`TextOutA`** is a no-op (`gfx.c`). Not seen called yet; if a screen
      draws text with it, render with SDL_ttf into the indexed screen.
- [ ] **Printing** (postcards, pictures): `CreateDCA` returns no printer.
      Build a printer DC that collects the `StretchDIBits` page and writes a
      PNG or PDF to the user's Pictures folder, or hands it to the macOS print
      dialog.
- [ ] **`StretchDIBits`** handles 8-bit and 24-bit sources to the screen only;
      1/4/16-bit and RLE are refused with a log line.
- [x] **Mouse pointer** (2026-11-15): drawn into the picture by the host
      (`gfx.c`), from the game's own cursor resources, so it scales with the
      window and needs nothing from the window system. Shown in headless
      screenshots too. Still to confirm by eye in the real window.
- [ ] **Volume / mute**, and pausing when the window loses focus.
- [x] **Window close** (2026-11-15): the close button / Cmd+Q now reaches the
      game as `SC_CLOSE`, so it asks before quitting unsaved, and message
      boxes are real dialogs (`SDL_ShowMessageBox`). The dialog itself has
      only been exercised headless (auto-answered): confirm it by eye.
- [x] **Remembered name** (2026-11-15): the newest save's name is typed into
      the sign-in book (`remember_name`, on by default). A picker for several
      players' names is not built: the book would need text drawn on its
      right-hand page.
- [ ] **A way to save without quitting.** The game only saves on MENU → QUIT.
      Options, cheapest first: (a) make Esc in a scene open the game's own
      menu, by having the host click the MENU button; (b) a host "save and
      keep playing" key that calls the game's save routine directly (near
      `0x401830`; needs its preconditions understood first); (c) save
      automatically when the window is closed instead of asking. (b) and (c)
      change how the original behaves: decide before building.

## 3. Making it something a non-programmer can use

- [x] **Launcher app** (2026-11-15): `make app` builds a SwiftUI launcher
      (`launcher/`) around the engine: disc chooser (ISO or folder), settings,
      saved games, Play. Layout checked from its own window snapshot; the
      controls have not been clicked through by a person yet.
- [ ] **A self-contained app**: SDL2/SDL2_ttf inside the bundle
      (`dylibbundler`), an app icon, code signing. Today it still needs
      Homebrew's SDL and is built from a terminal.
- [ ] **Launcher extras**: pick which saved game to continue (ties in with
      brief 01), volume, a screenshots browser, delete a save.
- [x] **Build from the launcher** (2026-11-15): choosing a disc the engine
      was not built for runs lift + compile from the app (needs the source
      checkout, compiler tools and Homebrew SDL). The rebuild path itself has
      only been run for the same disc; a second, different pressing of the
      game has never been tried.
- [ ] **The lift has to happen on the player's machine** (the lifted code is
      the game's and cannot be shipped). For an app that means either bundling
      the Python lifter, or loading a lift built at first run as a dylib.
      Decide which before starting on the bundle.
- [ ] **Read the ISO directly** instead of extracting 626 MB (mount with
      `hdiutil`, or a small ISO9660 reader behind `hle_set_drive`).
- [x] **Settings file** (2026-11-15): `madeline.ini` is read and written;
      F10/F11/F12 choices are remembered. A volume setting is still missing.
- [x] **Smoothing** (2026-11-15): on by default. `gfx.c` melts the 256-colour
      dithering with a colour-aware 5x5 average (ink lines are left alone) and
      the presenter scales smoothly, blurred bars when full screen. Pixel-art
      scalers (xBR, hqx) were tried on a captured frame and rejected: they
      turn the dither into a wormy texture.
- [x] **xBR after smoothing** (2026-11-17): `upscale=xbr` (F8, a box in the
      Windows launcher), off by default. `src/xbr.c` doubles the smoothed
      picture with Hyllian's xBR rule; on the picture after smoothing it does
      not make the worms it made of the raw dither. About 7 ms for a whole
      screen on 8 threads (Windows 11 desktop), changed parts only. It reaches
      the presenter through its 16-bit compose hook, so the mouse is
      untouched. Chosen over Anime4K after a side-by-side on six screens
      (Anime4K's shaders run through ffmpeg's libplacebo): Anime4K kept more
      brush texture but did less for the characters' outlines, its sharpest
      mode damaged small text, and it needs a neural network on the CPU or a
      shader path. Checked: headless shots, one look at a real window on
      Windows, a save and quit with it on. Not yet: played through with it
      on, the movies watched for speed, macOS or Linux built with it.
- [x] xBR in the macOS launcher (2026-11-21): a toggle under Picture, as in
      the Windows one. macOS builds with xBR and headless shots are right
      (1280x960); a real window with it on has not been looked at yet.
- [ ] xBR, left: 3x or 4x for very large screens; Anime4K as a second
      choice if a shader path ever exists.
- [ ] `scripts/playtest.sh` cannot make its contact sheet with ffmpeg builds
      that lack file-pattern input (MSYS2's): the shots are still written.
- [x] **Intro skip** (2026-11-15): `--skip-intro` or `skip_intro=1` in
      `madeline.ini`. The original already stops a movie on Enter; the host
      presses it for the two opening movies. Uses two addresses from this
      build of the EXE (`shims.c`).
- [ ] Code signing / notarization, if it is ever distributed as a binary.
      (Distribution has to stay "bring your own disc".)

## 4. Other platforms

Order: Linux, then Windows. Full checklists are in the briefs; the summary:

**Linux** ([brief 04](briefs/04-linux-build.md)) - expected to be close:

- [x] Per-system data folder (`src/platform.c`, XDG on Linux) and scripts
      that do not need macOS tools (2026-11-15; verified on macOS only).
- [ ] First build and win32hle selftests on Linux; guest memory base address.
- [ ] Headless playtest to the station (case-sensitive filesystem).
- [ ] Window, sound, pointer checked on X11 and Wayland.
- [ ] MIDI through FluidSynth.
- [ ] Launcher or not; desktop entry; CI without a game.

**Windows** ([brief 05](briefs/05-windows-build.md)) - a real port:

- [ ] Interim: the Linux build under WSL2/WSLg, documented. (Skipped: the
      real port below was done first.)
- [x] MinGW-w64 build of the same host (not pcrecomp's `native32`): a POSIX
      compatibility layer in `src/win/` for win32hle (`mmap` on
      `VirtualAlloc`, no sockets, small libc gaps). Built in MSYS2 UCRT64 on
      Windows 11 x64 (2026-11-17); headless playtests pass: start-up, menu,
      sign-in, new game to the Paris station, quit with a save, "old game".
- [x] Started in a window by a person and reported good, picture and sound
      (2026-11-17, Direct3D 11 and WASAPI through sdl2-compat). That was one
      short look: the F-keys, the close dialog and a long sitting have not
      been gone through one by one.
- [x] MIDI through the real `winmm` (`src/win/midi_win.c`, MCI sequencer).
- [x] **Launcher** (2026-11-17): `launcher/win/launcher.c`, the Mac
      launcher's window on plain Win32 (disc image or folder, settings, saved
      games, Play, build the engine for another disc). `make app` puts it in
      `build/app/` with the engine and the DLLs it needs. Checked by pictures
      of its window and by pressing its controls from a script against a test
      folder: unpacking a disc image, the settings file, Play (a headless game
      to the station and a save), the build. Nobody has used it by hand yet.
- [ ] Launcher, left: the program file has no icon of its own in Explorer
      (the window's is drawn at run time); a disc folder with a space in its
      path cannot be given to `make` (a user name with a space puts one in
      `%APPDATA%`); the window is laid out for the main display's scaling
      only.
- [ ] A version to hand out: it must lift on the player's machine, which
      today needs MSYS2 and this checkout. CI job; win32hle's selftests on
      Windows.
- [ ] Paths with non-ASCII characters (a user folder named in another
      script): the compatibility layer uses the ANSI file calls.

**Intel Macs**: should work unchanged; untested.

## 5. Housekeeping

- [ ] **First commit.** Nothing is committed yet (submodule staged, the rest
      untracked).
- [x] **CI** that builds the host without a game (2026-11-17):
      `.github/workflows/build.yml`. `make STUB=1` puts an empty lift in
      `build/stub`, so `src/` compiles and links against the pinned pcrecomp;
      `scripts/smoke.sh` checks the program starts; the launchers are built.
      macOS and Windows must pass; Linux runs but may fail until brief 04.
- [ ] CI, left: nothing in it plays the game (it has none). Tests that need
      no game would add to it: xBR on a made-up picture, the settings file,
      win32hle's selftests.
- [ ] **Pin and bump pcrecomp deliberately**: the submodule tracks a commit;
      upstream moves fast (see its CHANGELOG) and an update can change the
      lifter's output or a shim this host relies on.
- [ ] A LICENSE for this repository's own code (pcrecomp is MIT).

## Upstream (things written here that belong in pcrecomp's win32hle)

Offer these to sp00nznet/pcrecomp rather than keep them private forever:

- `RegQueryValueA` (the Win16-style call) in `advapi32.c`.
- `ShowWindow(SW_SHOWMAXIMIZED)` sizing the window to the screen.
- A `waveOut` that plays, with `WHDR_DONE` on playback (`winmm.c`'s is silent).
- `mciSendCommandA` for the `sequencer` device.
- `WING32.DLL`, and `StretchDIBits` for 8-bit DIBs with `DIB_PAL_COLORS`.
- A Windows (MinGW-w64) host for win32hle: `src/win/` is that, kept outside
  the submodule. Upstream it would be `_WIN32` branches in `guest_mem.c`
  (`VirtualAlloc`), `hle_path.c` (drive-letter paths, `d_type`),
  `kernel32_ext.c` (`st_mtim`, `tm_gmtoff`, `statvfs`) and `kernel32.c`
  (the program's own path), and `wsock32.c` on Winsock.
- A real palette-device mode (`RC_PALETTE`, `AnimatePalette` reaching pixels
  already on screen); `gfx.c` is one self-contained way to do it.
- `GetDriveTypeA` able to report a mapped drive as a CD-ROM.
- A presenter that takes a host-made picture larger than the screen for a
  32-bit surface: `hle_screen_compose_hook` is only asked for 16-bit ones,
  so `gfx.c` gives it a 16-bit stand-in screen while xBR is on.
- An idle throttle for games that spin on `PeekMessage`/`timeGetTime`.

## Out of scope

`SETUP.EXE`, the AOL "Wonderlink" sign-up, the screen saver (`SSAW`), and the
demos on the disc: all 16-bit Windows programs, separate from the game.

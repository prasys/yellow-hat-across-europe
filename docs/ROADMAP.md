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

- [ ] **`Madeline.app`**: a bundle with SDL2/SDL2_ttf inside (`dylibbundler`),
      an icon, and a first-run step that asks for the user's ISO or CD folder
      and remembers it. Today it needs Homebrew and a terminal.
- [ ] **The lift has to happen on the player's machine** (the lifted code is
      the game's and cannot be shipped). For an app that means either bundling
      the Python lifter, or loading a lift built at first run as a dylib.
      Decide which before starting on the bundle.
- [ ] **Read the ISO directly** instead of extracting 626 MB (mount with
      `hdiutil`, or a small ISO9660 reader behind `hle_set_drive`).
- [ ] **Settings file**: `madeline.ini` is read (`skip_intro`, `fullscreen`)
      but never written: F11/F12 choices and a volume are not remembered
      (`hle_screen_settings_hook` exists for this).
- [x] **Intro skip** (2026-11-15): `--skip-intro` or `skip_intro=1` in
      `madeline.ini`. The original already stops a movie on Enter; the host
      presses it for the two opening movies. Uses two addresses from this
      build of the EXE (`shims.c`).
- [ ] Code signing / notarization, if it is ever distributed as a binary.
      (Distribution has to stay "bring your own disc".)

## 4. Other platforms

- [ ] **Linux**: expected to build as is (64-bit, based memory). Needs a MIDI
      player in `midi.c` (FluidSynth + a SoundFont) and a save folder that is
      not `~/Library/...` (`main.c` hard-codes it; use XDG).
- [ ] **Windows**: pcrecomp's `native32` runtime would forward to real Win32,
      but WinG and 256-colour palettes no longer exist there, so `gfx.c` is
      still wanted. Unexplored.
- [ ] **Intel Macs**: should work unchanged; untested.

## 5. Housekeeping

- [ ] **First commit.** Nothing is committed yet (submodule staged, the rest
      untracked).
- [ ] **CI** that builds the host without a game: the lift cannot run in CI,
      so it needs a stub `build/gen` (an empty dispatch table) to at least
      compile and link `src/` against the pinned pcrecomp.
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
- A real palette-device mode (`RC_PALETTE`, `AnimatePalette` reaching pixels
  already on screen); `gfx.c` is one self-contained way to do it.
- `GetDriveTypeA` able to report a mapped drive as a CD-ROM.
- An idle throttle for games that spin on `PeekMessage`/`timeGetTime`.

## Out of scope

`SETUP.EXE`, the AOL "Wonderlink" sign-up, the screen saver (`SSAW`), and the
demos on the disc: all 16-bit Windows programs, separate from the game.

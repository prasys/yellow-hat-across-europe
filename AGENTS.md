# AGENTS.md

Guidance for coding agents (and people) working in this repository.

## What this is

**The Little Girl in the Yellow Hat Across Europe**: a native macOS port of
*Madeline: European Adventures* (Creative Wonders, 1996) made by static
recompilation. The project goes by its own name; the game keeps its title. The game's `MADEURO.EXE` is lifted to C by
[pcrecomp](https://github.com/sp00nznet/pcrecomp) (`third_party/pcrecomp`, a git
submodule) and linked with the host in `src/`, which stands in for Windows 95.
Read `README.md` first, then `docs/NOTES.md` (what is known about the
executable) and `docs/ROADMAP.md` (what is left to build).

Work that is ready to be picked up is written as briefs in `docs/briefs/`;
`docs/briefs/README.md` also explains how to hand one to another agent with
Orca. If you were given a brief, its "Done when" list is your definition of
finished.

## Hard rules

0. **The launcher draws its own artwork** (a hat from two shapes). No art,
   sound, character likeness or logo from the game goes into the app or the
   repository, and the built `.app` (it contains the lift) is never shared.
1. **Never commit anything that comes from the game.** That means the disc
   (`game/`), the lifted C (`build/gen/`), screenshots of the game, extracted
   art or sound, and long disassembly listings. `game/` and `build/` are
   git-ignored; keep it that way. Short instruction snippets in notes, to
   explain a finding, are fine.
2. **Do not edit `third_party/pcrecomp`.** It is someone else's repository.
   Answer a missing or wrong Win32 call in `src/` instead (see "Overriding a
   Win32 call"). If a fix belongs upstream, note it in `docs/ROADMAP.md`.
3. **Do not patch the game's behaviour** unless asked. The aim is the original
   game running correctly, quirks included. Conveniences are host-side and
   opt-in (`--skip-intro` presses a key the game already honours).
4. **Players' saves are real files** in
   `~/Library/Application Support/MadelineEuropeanAdventures/`. Never run a
   test against that folder and never delete from it. `scripts/playtest.sh`
   uses `OUT/save` (`MADELINE_SAVE_DIR`); do the same in anything new.
5. `make run` opens a window and plays sound on the user's machine. Prefer the
   headless playtest for checking work; say so before launching the real thing.

## Commands

```sh
make extract ISO=/path/to.iso   # unpack a disc into game/cd (needs the user's own ISO)
make                            # lift (if needed) + build build/madeline   (~15 s clean)
make lift                       # re-run the recompiler only
make run                        # play, in a window
make app                        # build/<project name>.app: launcher + engine (make launch opens it)
make clean                      # removes build/ (the lift too)
scripts/playtest.sh OUT "SCRIPT" [speed]   # headless, fast-forwarded, screenshots + _sheet.png
scripts/dis.sh 405b90 405c10    # original instructions for an address range, from the lift
```

Requirements: `brew install sdl2 sdl2_ttf`, Python 3 with `capstone` (the lift
script makes `.venv` itself), and `ffmpeg` if you want contact sheets.

There is no unit-test suite. "It builds" is not verification: run a playtest
that reaches the code you changed and look at the screenshots.

## Layout

| Path | Role |
|---|---|
| `src/main.c` | Entry: drive mapping (`D:` = disc, `C:` = save folder), shim registration order, boot |
| `src/gfx.c` | All of GDI32 + WING32 + `GetDC`/`ReleaseDC`, on a palette-indexed 640x480 screen |
| `src/audio.c` | `waveOut*` on SDL (via win32hle's stream mixer) and `mciSendCommandA` |
| `src/midi.c` | MIDI files on AudioToolbox; silent stand-in elsewhere |
| `src/shims.c` | CD drive type, registry, the clock + idle throttle, window maximize |
| `src/debug.c` | `MADELINE_SCRIPT` test driver; F9 / `SIGUSR1` screenshots |
| `src/madeline.h` | What the files above share |
| `launcher/` | SwiftUI app (macOS 13+): edits `madeline.ini`, lists saves, unpacks a disc image, runs the engine. Built with plain `swiftc` (`make app`), no Xcode project. `launcher --snapshot FILE` writes a picture of its window, for checking layout |
| `scripts/` | extract, lift, playtest, dis |
| `docs/` | NOTES (findings), ROADMAP (to do), `briefs/` (work orders), `MADEURO.sha256` (the build targeted) |
| `build/gen/` | Generated: the lifted game. Never edit by hand, never commit |

## How the host works (the parts that bite)

- **The engine is tied to one MADEURO.EXE.** `build/gen/lift_id.h` holds the
  POSIX cksum and size of the EXE that was lifted; `main.c` refuses any other
  (exit 3) and `--check` reports it. A different disc must be re-lifted
  (`make CD=...`); the launcher does this itself through
  `make engine-into-app`, using the checkout path stored in the app
  (`Contents/Resources/source-root`).
- **Shim ABI.** A Win32 import is a `void fn(void)` run inside the lifted
  machine. Read stdcall arguments with `A32(i)`, `APTR(i)`, `ASTR(i)`; return
  with `RET(value, argc)`. **`argc` must be the real argument count** or the
  guest stack is corrupted and the crash shows up somewhere unrelated.
  `wsprintfA`-style cdecl calls are the exception (caller cleans).
- **Guest memory is based, not flat.** Guest addresses are 32-bit offsets into
  a 4 GB mapping (`RECOMP_MEM_BASE`). Use `MEM8/16/32(va)`, `hle_ptr(va)`,
  `hle_va(ptr)`. Anything the guest will hold a pointer to must be allocated
  with `hle_alloc`, not `malloc`.
- **Overriding a Win32 call.** `win32hle_register` keeps every table and
  `hle_resolve` returns the *first* match by name, so this project's tables are
  registered in `main.c` **before** `recomp_host_init()`. To override a call,
  add it to a table in `src/`. To pass a call on to the stock version (a
  wrapper), use `stock_shim_va()` + `stock_shim_call()` in `shims.c`; never
  cache the function pointer `recomp_lookup_import` returns. To use a
  different stock shim from host code, call it as the guest would:
  `hle_call_guest(hle_resolve("MoveWindow"), 6, args)`.
- **The game waits by spinning**, on `timeGetTime` or on `PeekMessage`.
  `idle_tick()` in `shims.c` is called from both; it throttles the spin and is
  the only place sound (`madeline_audio_tick`) and the test script advance. A
  new wait loop that calls neither would stall both: check with
  `MADELINE_TRACE=1` what a stuck screen is calling.
- **Animation is paced by sound.** The game polls `WHDR_DONE`; a buffer must be
  marked done when it has been *played*. Do not "simplify" this to immediate
  completion: movies and speech would run at full speed.
- **The mouse pointer is drawn by the host** into the output picture
  (`madeline_gfx_pointer`, called from the idle tick), not by SDL. The system
  cursor stays hidden over the window. `MADELINE_NOPOINTER=1` leaves it out of
  screenshots.
- **Mouse.** The game hit-tests `GetCursorPos` (screen coordinates) against a
  mask picture, and ignores clicks while a character is speaking.

## Debugging loop

1. Reproduce headless: `scripts/playtest.sh /tmp/out "…; 200000 shot x.bmp; 201000 quit"`.
   Times are game-clock milliseconds; the default speed is 8x. Set
   `MADELINE_SKIP_INTRO=1` to reach the main menu in a few seconds (otherwise
   the intro ends around 60000). Leave generous gaps: input is dropped during
   speech, so a step that "did nothing" was often just early. Look at
   `_sheet.png`.
2. `FATAL: unimplemented import X` → implement `X` in `src/`.
   `no lifted function at 0x…` → the recompiler missed a function reached only
   through a pointer; it needs seeding at lift time (see pcrecomp's
   `disasm32 --seed-functions` and `seed_from_log.py`), not a host change.
3. Switches: `MADELINE_TRACE=1` (every import), `HLE_FILETRACE=1`,
   `HLE_MSGTRACE=1` (dispatched messages; mouse messages are not shown),
   `MADELINE_GFXTRACE=1`, `MADELINE_AUDIOTRACE=1|2`. Script verbs: `click`,
   `move`, `key`, `type`, `shot`, `peek ADDR`, `win`, `close`, `quit` (`src/debug.c`).
4. To see what the game does at an address, `scripts/dis.sh LO HI`. Import
   thunks are the `jmp dword ptr [0x4233xx]` stubs near `0x41CE8C`-`0x41D110`.

## Conventions

- C, 4-space indent, long lines are fine; match the surrounding file.
- Comments say *why* the game needs something and how that was learned, not
  what the next line does. A new finding about the executable also goes in
  `docs/NOTES.md`.
- Keep host code about this game. Generic Win32 behaviour that pcrecomp lacks
  is written here for now and listed under "Upstream" in the roadmap.
- Commit only when the user asks. When something is finished or newly found
  broken, update `docs/ROADMAP.md` and the "What works" section of the README
  in the same change.

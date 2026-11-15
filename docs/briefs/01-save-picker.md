# 01 - Save picker on the sign-in book

## Goal

Let a player with more than one saved game choose which to continue, without
having to remember how they spelled their name.

## What is known

- The game finds a save only by the exact name typed into the sign-in book
  (screen 39). It never lists names. See "Keys, movies and saving" in
  `docs/NOTES.md`.
- A `.sav` starts with the name (NUL-terminated, at most 10 characters); a
  non-zero word at `0xF0` marks a dead file. Slots are `0.sav`-`9.sav`.
- `src/shims.c` already has `last_player()` (reads the names) and
  `remember_name_tick()` (types the newest name into the book one verified
  letter at a time, using the guest's own name buffer at `[0x421D9C]`).
- The right-hand page of the book is empty in the original: free space.
- The host draws into the output picture after the game (`src/gfx.c`: the
  pointer is drawn that way). There is no text renderer in `gfx.c` yet;
  SDL2_ttf is already linked (win32hle uses it).

## Build

1. When the sign-in book is up and there are two or more saves, show the saved
   names on the right-hand page, newest first, drawn by the host over the
   game's picture (not into the game's own bitmap).
2. Clicking a name replaces whatever is in the NAME line with it: Backspace
   until the guest buffer is empty, then type it, verifying each letter the
   way `remember_name_tick` does. The click must not also reach the game.
3. With one save, behave as today (pre-fill). With none, show nothing.
4. `remember_name=0` in `madeline.ini` turns the whole thing off.
5. Add a `saves` verb or similar to `src/debug.c` only if a test needs it.

## Done when

- `scripts/playtest.sh` run with two saves copied into `OUT/save/CWONDERS/MADEURO/`
  shows both names on the book in a screenshot.
- A scripted click on the older name, then the red arrow (560,440), then
  "old game" (300,180) loads that save (its scene's files appear in `run.log`).
- A run with no saves and a run with `MADELINE_NO_NAME=1` look exactly as before.
- README "Playing" and `docs/ROADMAP.md` are updated.

## Out of scope

Deleting or renaming saves. Changing the save format. Drawing anything on
other screens.

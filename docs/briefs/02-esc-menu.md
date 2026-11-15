# 02 - Esc opens the game's menu from a scene

## Goal

Saving is only possible through MENU -> QUIT, and MENU is a small button at the
bottom right. Make Esc do what clicking MENU does, so the way out (and the
save) is one key away.

## What is known

- Inside a scene the game ignores every key (`docs/NOTES.md`, key table). The
  current screen is the word at `0x41E540` (`MADnnn / 10`); scenes are the
  values that are not 0, 13, 14, 34, 38, 39.
- MENU is at about (615,435) in the 640x480 picture, on the inventory bar.
  Whether the bar is in the same place on every scene has **not** been checked.
- Clicks are ignored while a character speaks (`0x421C80` is 0 then) and while
  a movie plays (`0x421D84` non-zero).
- `hle_screen_key_hook` (already used for F9 in `src/debug.c`) sees keys before
  the game; `hle_input_mouse` injects a click. The window procedure's own
  click path reads `GetCursorPos`, so the injected click must move the cursor
  there, and should put it back afterwards.

## Build

1. Esc in a scene, with input enabled and no movie, clicks MENU for the player.
2. Esc during a movie keeps its present meaning (the game stops the movie).
3. Esc on the options screen (38) clicks RETURN TO GAME (about 180,430), so the
   key toggles. Skip this step if the first sign-in visit to that screen makes
   it ambiguous, and say so.
4. Opt-out: `esc_menu=0` in `madeline.ini`.

## Done when

- A playtest that reaches the station (see `docs/NOTES.md`, session flow),
  waits for speech to end, sends `key 27`, and screenshots shows the options
  screen; `MAD380` files appear in `run.log` after the key.
- The same on at least two other scenes, to settle the "same place everywhere"
  question; record the answer in `docs/NOTES.md`.
- Esc during the opening movies still skips them.

## Out of scope

A save-without-quitting key (that needs the game's save routine understood;
separate brief).

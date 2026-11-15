# 03 - Scripted walk-through tests

## Goal

Only the first scene has been exercised. Build the means to walk the rest of
the game headless and to notice when a change breaks something.

## What is known

- `scripts/playtest.sh` + `MADELINE_SCRIPT` (`src/debug.c`) drive the game by
  fixed game-clock times. Fixed times are fragile: speech blocks input for an
  unknown while, and the 8x clock stretches file I/O relative to game time.
- `HLE_FILETRACE=1` logs every file the game opens; a screen change always
  opens `MADnnn/MADnnn.DAT`. That is the reliable signal of "we got there".
- Failures to expect on new screens: `FATAL: unimplemented import`, `no lifted
  function at 0x...` (needs seeding at lift time, see `AGENTS.md`), or a stall
  (a wait loop the idle hook in `src/shims.c` does not see).
- 45 `MADnnn` folders exist; entered so far: 000, 050, 130, 380, 390.

## Build

1. New script verbs in `src/debug.c`: `wait-file NAME [timeout]` (resume when
   the game opens a file whose path contains NAME), `wait-input` (resume when
   `0x421C80` is non-zero and no movie plays), and relative delays
   (`+2000 click ...`). A timeout fails the run with a non-zero exit.
2. `tests/*.script` and `scripts/run-tests.sh`: at least sign-in -> new game ->
   station -> menu -> quit (asserting a `.sav` is written), and the load path.
3. Then explore: one script per location reached, noting in
   `docs/NOTES.md` what each `MADnnn` is and how to get there. Record every
   crash or stall found in `docs/ROADMAP.md`; fix those that are host-side.

## Done when

- `scripts/run-tests.sh` passes twice in a row on an unchanged tree, and fails
  when `src/audio.c` is made to mark buffers done immediately (the classic
  regression): show both.
- No test uses the player's real save folder (`MADELINE_SAVE_DIR`).
- At least ten locations beyond the station have been entered, or the blocker
  to going further is written down.

## Out of scope

Committing screenshots or any other game content as "expected" images:
compare file-open sequences and exit codes, not pictures.

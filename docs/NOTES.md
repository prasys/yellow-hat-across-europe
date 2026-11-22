# Notes on MADEURO.EXE

What the port needed to know, and how each thing was found.

Status (2026-11-15): running on macOS; see `ROADMAP.md` for what is left and
`../AGENTS.md` for how to work on it.

## The binary

- PE32, GUI, image base `0x400000`, entry `0x401000`, Borland C++ 1995 (its
  runtime asks kernel32 for an export named `Borland32`, which is not there on
  Windows either). 118 KB of code; pcrecomp finds 932 functions, 0 lift errors.
- SHA-256 of the build this was made against is in `MADEURO.sha256`;
  `scripts/lift.sh` warns if yours differs.
- 110 imports from six DLLs: KERNEL32 (49), USER32 (27), GDI32 (19), WINMM (8),
  WING32 (4), ADVAPI32 (3). It was written for Windows 3.1 + Win32s and
  Windows 95 alike, so it uses nothing newer than either.
- Resources: one icon and eight monochrome 32x32 cursors (AND/XOR masks). The
  game selects them with `SetCursor` (an arrow, and a different one over
  something clickable) and hides the pointer with `ShowCursor` during movies.
  Handing them to SDL as system cursors showed nothing in the real window, so
  the host draws the pointer into the picture itself (`gfx.c`).
  Which one is chosen from the mask byte under the pointer (from 0x403140 on):
  resource 6 is the arrow, 2 the girl over something clickable, 7, 8 and 10
  for other kinds of spot. `LoadCursorA` is called again at every change, so
  it must give back the same handle each time (`shims.c`).

## Start-up checks

1. **The disc.** `WM_CREATE` asks `GetDriveTypeA` about `C:\` to `Z:\` and takes
   the first `DRIVE_CDROM` (5) holding `\MAD000\SCRN000.PCX`; otherwise
   "Madeline CD-ROM missing". The host answers 5 for `D:`.
2. **The installation.** It opens `HKEY_CLASSES_ROOT\CreativeWonders`, then
   `MadelineEuropeanAdventures` under it, and reads the default value with
   `RegQueryValueA`: the install folder. `INSDATA/SETUP.INS` shows the installer
   writing `C:\CWONDERS\MADEURO` there. Saved games (`0.sav` to `9.sav`) and ten
   `SCRATCHn.TMP` files (copies of the postcard pictures in `MAD320`, removed on
   exit) live in that folder.
3. **The screen.** `GetDeviceCaps` is asked whether this is a palette device; if
   not, a message box warns that performance will suffer. The host is one.
4. `mad400\palanim.dat` is opened by relative name and is not on the disc. The
   original fails to open it too.

## The window

Created 0 by 0 with `WS_POPUP | WS_VISIBLE`, then `ShowWindow(SW_SHOWMAXIMIZED)`.
win32hle does not size a window on maximize, and a window with no area never
gets mouse messages, so the host moves it to 640x480 the first time the game
pumps. Mouse positions come from `GetCursorPos` (screen coordinates) minus the
window's origin, tested against a per-screen mask picture (`MSCRNnnn.PCX`).

## Drawing

One 640x480 top-down 8-bit `WinGCreateBitmap`, drawn into directly, shown with
`WinGBitBlt` of the changed rectangle. Colours: `WinGSetDIBColorTable`, then
`SetPaletteEntries` / `SelectPalette` / `RealizePalette` / `AnimatePalette` on a
255-entry palette. Fades are done with the palette alone, with no blit, which
is why the host keeps the screen as indices and re-colours it on every palette
change rather than converting at blit time.

## Time and sound

- The game waits by spinning: on `timeGetTime`, or on `PeekMessage` with an
  empty queue (the options screen). The host sleeps out a spin within one
  millisecond from both places; without that it holds a core at 100%.
- `waveOutOpen` is 22050 Hz, mono, 8-bit, with no callback: the game polls
  `WHDR_DONE` in the headers it wrote and paces movie frames and lip movement
  by it. The flag is therefore set when the samples have been played, not when
  they were queued.
- Music is `mciSendCommandA` on the `sequencer` device with
  `MAD400\MEAMIDn.MID`, played with `MCI_NOTIFY`. The game restarts it on
  `MM_MCINOTIFY` only inside the wait loop at `0x4111xx` (a `PeekMessageA`
  for `0x3B9` alone, then `0x41016D` when `[0x41E538]` is 1). The window
  procedure (`0x40665B`) ignores the message, so on the new/old game menu
  (`MAD380`, `MEAMID2`, 89 s) the music ends and stays off, as on Windows.
  The title screen goes on to the sign-in book by itself about 25 s
  (game clock) after its music starts.
- The disc's ISO 9660 image: libarchive 3.8 (`bsdtar`, on Linux and in MSYS2)
  lists it as empty without an error; 7-Zip reads it. `extract_iso.sh` tries
  7z when bsdtar left no `MADEURO.EXE`.

## File formats seen (not decoded here: the game's own code reads them)

`.MAV` speech and animation, `.AVC` movies, `.PCX` backgrounds and masks,
`MADnnn.DAT` per-screen scripts, `.COL` palettes.

## Flow of a session (game-clock times, for writing test scripts)

| When | What | Files it opens |
|---|---|---|
| 0 s | Two opening movies; a key stops each | `MAD000/IRONSMAL.AVC`, `IMMADSM.AVC` |
| ~58 s | Main menu (Madeline in the middle); music starts | `MAD000/SCRN000.PCX`, `MEAMID4.MID` |
| click Madeline | Sign-in book: type a name, click the red arrow | `MAD390/*`, one `327-27?.mav` per letter |
| | Options: new game / old game / Wonderlink / credits / return / quit | `MAD380/*`, `MEAMID2.MID`, probes `0.sav`-`9.sav` |
| new game | Opening movie, then the Paris station | `MAD000/OPENSMAL.AVC`, then `MAD050/*` |
| MENU (bottom right, in a scene) | Back to options; QUIT there saves and leaves | writes `n.sav`, `MAD130/*` (goodbye) |

Left alone, the main menu moves on by itself after a while. Input is ignored
while a character is speaking, on every screen.

## Keys, movies and saving

The window procedure handles `WM_CHAR` only (`0x406384`, helper `0x402EE1`),
and what a key does depends on the current screen, the word at `0x41E540`
(the `MADnnn` number divided by ten: 0 main menu, 5 the station, 13 goodbye,
38 options, 39 the sign-in book):

| Screen | Key | Effect |
|---|---|---|
| any | Enter, while a movie plays | stops the movie (`0x40E7D6`) |
| 0 (opening, main menu) | any key but Space, while a movie plays | stops the movie |
| 0 | Esc; or Enter / Space with no movie | go to the sign-in book (screen 39) |
| 13 (goodbye) | any | finish at once |
| 14, 34, 39 | text | typed into the screen (the name, in 39) |
| a scene (5, ...) | any, Esc included | nothing |

Screen 34 is the Word Game (`MAD340`): it draws a keyboard of its own to
click, so it can be played with the mouse alone. Screen 14 has not been
reached, so what it types into is not known. `PeekMessageA` is the only call
the game reads messages with (no `GetMessage`, no `GetAsyncKeyState`): a
host that wants a message for itself can take it there (`m_PeekMessageA` in
`shims.c` does, for the keyboard the host draws).

`0x421D84` is non-zero while a movie plays; `0x41E1CC` counts the start-up
steps (1 and 2 as the two opening movies start). `--skip-intro` watches those
two and presses Enter; it changes nothing in the game.

Saving:

- `%s\%d.sav` in the install folder, `0.sav` to `9.sav`, about 230 KB each:
  one per name in the sign-in book. The options screen probes all ten when it
  opens (that is how "old game" knows whether to be greyed out).
- A save begins with the player's name (as typed, at most 10 characters,
  NUL-terminated); a non-zero word at `0xF0` marks a file the game deletes.
  `0x407475` finds a slot by comparing that name with the one in the book,
  and it is the only reader: **the game never shows a list of names**, and
  "old game" is lit only for an exact match. The book's pages are blank apart
  from the NAME line in the original too.
- Name entry (`0x40E5B0`, screen 39): letters, digits, Space, `.`; Backspace
  deletes; Enter continues. The text lives at the start of the player record
  `[0x421D9C]`. A key pressed while the book is still asking for the name is
  lost; later keys queue, each taking about a second (the letter is spoken).
  The host's `remember_name` types the newest save's name there, checking
  that record after each letter.
- **A save is written only by QUIT on the options screen** (reached with the
  MENU button in a scene). Nothing is written on entering a scene, on opening
  the options, or on a timer. Checked by killing the process in a scene after
  opening the menu: no file.
- The other way out is `WM_SYSCOMMAND`/`SC_CLOSE` (Alt+F4 on Windows): the
  game asks "Exiting this way will NOT save your game... Are you sure you want
  to quit?" (`MB_YESNO`) and on Yes destroys its window. It has no `WM_CLOSE`
  handler, so the host converts the presenter's `WM_CLOSE` (close button,
  Cmd+Q) into that message, and shows the question as a real dialog.

## Printing

One routine, `0x40FBB0(bits, width, height)`, called from two places: the
painting screen's Print button (screen 32, mask zone `0x71`, a 596x349
picture) and a second one not yet reached in play (`0x4155B0`, in the click
handler `0x415415`, zone `0x72`, a 548x290 picture: the postcards, by the
size, but which screen is a guess; zone `0x72` is in the masks of 4, 14 and
33). Both first play
`mad400\printwt.mav`. Then:

1. `MessageBoxA` "Would you like to print? Select YES to print" (Yes/No).
2. `0x40FAEC`: `GetProfileStringA("windows", "device", ",,,")`, the default
   printer of WIN.INI, cut at its commas into name, driver, port (any part
   missing: "Bad Driver Information In 'WIN.INI'"), and
   `CreateDCA(driver, name, port, NULL)`. There is no print dialog.
3. `GetDeviceCaps(RASTERCAPS)` must have `RC_BITBLT`. Then four more checks
   that pass the *RC_ bit values* 8, 2, 1, 4 as the index, each with its own
   warning box when the answer is zero ("requires BANDING support"...): as
   indices those are HORZRES, TECHNOLOGY, nothing, and HORZSIZE. A box does
   not stop the print.
4. `StartDocA` (document "MADEURO"), `StartPage`, the picture turned upside
   down in place (`0x406EEE`) to make a bottom-up 8-bit DIB with the current
   colours as its table, one `StretchDIBits` to `0,0` at HORZRES wide by
   three quarters of HORZRES high (VERTRES if that is less), turned back,
   `EndPage`, `EndDoc`, `DeleteDC`.

Neither picture is 4:3, so the original printed them stretched tall. The host
keeps the picture's own shape (`print.c`).

`scripts/dis.sh` drops any instruction with a `*` in its text (`lea edi,
[edi + edi*2]`, the "three quarters" above, among them): the awk pattern
stops at the first `*`.

To reach it in a test (with `MADELINE_SKIP_INTRO=1`, speed 8):
`12000 click 320 280; 40000 type MAX; 52000 click 560 440; 86000 click 330 60;
95000 key 13; 110000 key 13; 205000 click 536 440` (the activities button),
`262000 click 300 80` (Painting), `300000 click 210 220; 322000 click 560 430`
(a picture, the arrow), `382000 click 114 468` (Print). The activities screen
is `MAD310`: Painting leads to `MAD350` then `MAD320`, Countries to `MAD370`,
Word Game to `MAD340`.

## Things that went wrong on the way (so they are not repeated)

- Clicks seemed to work before they did: early screens advance on a timer or
  by keyboard, and the 0x0 window was silently swallowing every mouse message.
  Check a click by its effect (a file opened), not by the screen changing.
- The options screen stalled the test driver because it idles on `PeekMessage`
  and never calls `timeGetTime`; the idle hook now covers both.
- pcrecomp's analysis tool lists import slots from the lookup table; Borland's
  call thunks jump through a second table at `0x4233xx`, so those addresses do
  not match the tool's.
- The lifted C needs `recomp_funcs.h` force-included (`-include`) to compile.
- macOS ships GNU Make 3.81: no grouped targets (`&:`).
- "The intro cannot be skipped" was wrong for an hour: the keys were pressed
  while the window was still 0x0 and unfocused. Enter and Esc both work.
- A capital typed by the host came out small: `hle_input_key` for Shift down,
  the letter, Shift up, all at once. win32hle picks the case when the game
  *translates* the key press, from Shift as it is then, and by then Shift
  was up again. `remember_name` had this from the start: a save signed
  "Max" got an "m" typed, which no longer matched, and the host gave up.
  `madeline_type` keeps Shift down until the game has read the key's
  release.
- `recomp_lookup_import(va)` returns one shared entry function and *selects*
  the shim as a side effect. Cache the address, not the function pointer:
  calling a cached pointer runs whichever shim was looked up last (here, the
  caller itself, forever).
- Tests once ran against the player's real save folder. `playtest.sh` now sets
  `MADELINE_SAVE_DIR` to its own output folder.

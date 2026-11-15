# Briefs

Self-contained work orders for handing a piece of this project to another
agent (or person). Each one says what to build, where, how to tell it is done,
and what not to touch. Read `../../AGENTS.md` first: its hard rules apply to
every brief.

| Brief | Size | Depends on |
|---|---|---|
| [01 Save picker](01-save-picker.md) | small | - |
| [02 Esc opens the menu](02-esc-menu.md) | small | - |
| [03 Scripted walk-through tests](03-walkthrough-tests.md) | medium | - |
| [04 Linux build](04-linux-build.md) | medium | - |

Not yet written up (see `../ROADMAP.md`): printing the postcards, a
`Madeline.app` bundle, reading the ISO without extracting it, save-and-continue.

## Handing one off with Orca

One brief, one worktree, one agent. From a terminal inside this repo's Orca
workspace:

```sh
orca worktree create --name save-picker --base-branch <branch with this work> \
  --agent <agent id> --json \
  --prompt "Read AGENTS.md, then do docs/briefs/01-save-picker.md. The extracted
game disc is not in your checkout: use the one at <path to game/cd> via
make CD=<path> and MADELINE_CD=<path>. Do not commit or push; stop and report
when the brief's 'Done when' list passes."
```

- `--agent` is the id Orca knows the agent by (`claude`, `codex`, and whatever
  else is installed; check the id for OpenCode on your machine before relying
  on it).
- `--base-branch` matters while this work is not on `main`: a worktree made
  from `main` would have none of the host code.
- `game/` and `build/` are git-ignored, so a new worktree has **no game and no
  lift**. Point it at an existing extraction (`CD=`, `MADELINE_CD=`); it will
  lift and build for itself in about 15 seconds.
- The result's `launch.outcome.handle` is the agent's handle and
  `launch.prompt.outcome` says whether the brief arrived. To look in later:
  `orca terminal read --terminal <handle> --json`.
- Briefs that edit the same file (01 and 02 both touch `src/shims.c`) will
  conflict if run at once. Run them one after the other, or expect a merge.

## Writing a new brief

Goal in one paragraph; the facts already known (addresses, files, what was
tried); files to touch; a "Done when" list that can be checked by running
something; what is out of scope. If it cannot be checked headless with
`scripts/playtest.sh`, say how it is to be checked instead.

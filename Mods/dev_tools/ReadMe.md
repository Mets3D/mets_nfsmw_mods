# Dev Tools

*Disclaimer: The code was written by Claude (Sonnet & Opus).*

A plugin (`nfsmw_dev_tools.asi`) for Need for Speed: Most Wanted (2005, PC, `speed.exe` 1.3) with things that are useful
while making mods for the game and not much outside of that. Leave it out of an install you only play. Each tool has its
own source file and its own switch in `nfsmw_dev_tools.ini`.

| Tool | What | Switch |
|---|---|---|
| [Diagnostics](#diagnostics) | where the game ends itself and where it has an access violation | `[diagnostics] enabled` |
| [Quick start](#quick-start) | skips the intro movies (`skip_intros`); with `auto_load_alias`, also the splash screen and the save choice | `[quick_start] enabled` |

All switches are 1 (on) by default; the ini written next to the game lists every option. Ideas for more tools (not written): auto-loading a save and landing in the world,
spawning cars, teleporting to locations, testing the AI in preset scenarios.

## Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)

## Diagnostics

`ExitProcess` and `TerminateProcess` (of the game) are logged with the places in the game's code the call came from, and
the first three access violations with their registers. (That is how the Direct3D failure of a second full screen copy was
found.)

## Quick start

Sets the same flag `NFSMWExtraOptions`'s "Skip Movies" and the widescreen fix's "Skip Intro" use: a byte the game checks
before opening any FEng movie (the EA logo, the PSA, the attract movie at startup), posting
that movie's "finished" message right away instead of playing it. It is cleared again after the attract movie, so the in-game
movies (the event types' "View Tutorial") play. Our plugin sets it a couple of seconds after the
game starts, so the first movie sometimes plays anyway; renaming a movie's file (the game then skips it) has no such
race.

**Auto load:** set `auto_load_alias=<save name>` under `[quick_start]` in `nfsmw_dev_tools.ini` (independent of `skip_intros=1`, which controls the movie skip). If a save with
that name exists, the splash screen ("click to continue") moves on by itself once it has loaded, and that save is
loaded straight away, even when there are several (no list to pick from); you land on its "Load complete" message.
Empty, or a name with no save: the game starts as usual.

## Ideas / TODOs
- A replay system, so we can spectate each other?
- A ghost system, so we can race against our best times?
- An options menu to tweak the current race's settings, like difficulty level, rubber banding (Catch Up), number of opponents, number of laps, traffic, police presence, and maybe even hidden values like opponent car value, however that is determined? The race would have to be restarted for the changes to take effect.
- Would be interesting to display the perspective of a CPU player in the corner of the screen - Or, maybe it would be much easier, to allow a CPU to take control of one of the split screen players - But how do CPU players work, do they give their cars inputs like a human player, or is it something else?

## Files

In the folder `scripts/mets_mods/`: `nfsmw_dev_tools.log` and an optional
`nfsmw_dev_tools.ini`.

| File | What |
|---|---|
| `main.cpp`, `dev_tools.h` | the entry: the log, the ini, each tool's switch |
| `diagnostics.cpp` | the exit and access violation logs |
| `quick_start.cpp` | the movie-skip flag, the splash-screen skip |

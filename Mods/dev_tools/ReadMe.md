# Dev Tools

*Disclaimer: The code was written by Claude (Sonnet & Opus).*

A plugin (`nfsmw_dev_tools.asi`) for Need for Speed: Most Wanted (2005, PC, `speed.exe` 1.3) with things that are useful
while making mods for the game and not much outside of that. Leave it out of an install you only play. Each tool has its
own source file and its own switch in `nfsmw_dev_tools.ini`.

| Tool | What | Switch |
|---|---|---|
| [Multiple instances](#multiple-instances) | lets the game start more than once on one PC | `[multiple_instances] enabled` |
| [Network](#network) | a log of the game's network calls | `[network] log` |
| [Diagnostics](#diagnostics) | where the game ends itself and where it has an access violation | `[diagnostics] enabled` |
| [Quick start](#quick-start) | skips the intro movies (`skip_intros`); with `auto_load_alias`, also the splash screen and the save choice | `[quick_start] enabled` |
| [Boot trace](#boot-trace) | a log of start-up: which screen, the save-directory check, whether the game draws | `[boot_trace] enabled` |

All switches are 1 (on) by default; the ini written next to the game lists every option. Ideas for more tools (not written): auto-loading a save and landing in the world,
spawning cars, teleporting to locations, testing the AI in preset scenarios.

## Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)

## Multiple instances

The game's `main()` starts with `IsTheGameAlreadyRunning("speed.exe", 1)`. That function lists the running processes,
counts the ones called `speed.exe` and, if there is another one, waits for it to go (a new count each second, three times)
and then ends the game.

The ASI loader starts plugins only after that first count, so the plugin cannot answer the question before it is asked. It
uses the waiting instead: while the game sleeps between two counts, the plugin makes the process list unavailable, which
the game takes for "nothing else is running". (The check is replaced as well, for a loader that starts plugins earlier.)
The plugin is in about 2 seconds after the process starts and the game gives up after about 3 more, so it has some margin,
but a very slow start could lose the race: the second copy then exits as before.

Starting two copies is only the first step; what happens next belongs to the game:
- **Windowed mode.** The vanilla game takes the screen exclusively (a Direct3D full screen device), and a second copy
  cannot create its own: it crashes soon after the check (an access violation at start-up). Both copies need a windowed
  mode; the widescreen fix's `WindowedMode` (1-5) does it, and two copies of the test install ran side by side this way.
- **The same profile.** Both copies save to `Documents\NFS Most Wanted\<profile name>`. Use a different profile in each,
  or one copy will overwrite the other's save.
- **Input.** Both copies read the same keyboard and controller. Only the window in front should react to the keyboard;
  a controller may drive both.
- **Logs.** Each copy writes its own log: the second one gets `name.2.log`, and so on (for all our mods).

## Network

The game finds LAN games with UDP broadcasts on port 9999 (384-byte packets that start with `67 45 41`) and plays over TCP
port 9900. The tool logs the game's Winsock calls (the first 800 lines: sockets, binds, sends, the packets received) and
the first announcements in full (the 384 bytes without their trailing zeros). It changes nothing.

LAN games work between two PCs (a Windows PC and a Linux laptop running the game in Proton were tried). Two copies on one
PC do not find each other: the second copy cannot bind port 9999, and sharing the port and hiding the copies' common address
from the game (both tried) did not make the host appear in the list.

## Diagnostics

`ExitProcess` and `TerminateProcess` (of the game) are logged with the places in the game's code the call came from, and
the first three access violations with their registers. (That is how the Direct3D failure of a second full screen copy was
found.)

## Quick start

Sets the same flag `NFSMWExtraOptions`'s "Skip Movies" and the widescreen fix's "Skip Intro" use: a byte the game checks
before opening any FEng movie (the EA logo, the PSA, the attract movie at startup, and any later in-game one), posting
that movie's "finished" message right away instead of playing it. Our plugin sets it a couple of seconds after the
game starts, so the first movie sometimes plays anyway; renaming a movie's file (the game then skips it) has no such
race.

**Auto load:** set `auto_load_alias=<save name>` under `[quick_start]` in `nfsmw_dev_tools.ini` (independent of `skip_intros=1`, which controls the movie skip). If a save with
that name exists, the splash screen ("click to continue") moves on by itself once it has loaded, and that save is
loaded straight away, even when there are several (no list to pick from); you land on its "Load complete" message.
Empty, or a name with no save: the game starts as usual.

## Boot trace

Writes what start-up is doing to `nfsmw_dev_tools.log`: each change of boot screen, of the save-directory check
(running, waiting for an answer to a message, and the message's text), and every 5 seconds how many frames the game
drew. It stops 10 seconds after the main menu, or after 3 minutes. "screen (none)" and "eFrameCounter +0" in every line
means the game has not started drawing at all (see `docs/project.md`, Testing the game).

## Files

In the folder `scripts/mets_mods/`: `nfsmw_dev_tools.log` (and `.2.log`... for later copies) and an optional
`nfsmw_dev_tools.ini`, also with `[hooks] alreadyrunning=0` to put the check back.

| File | What |
|---|---|
| `main.cpp`, `dev_tools.h` | the entry: the log, the ini, each tool's switch |
| `multiple_instances.cpp` | the check for another copy |
| `network.cpp` | the Winsock hooks |
| `diagnostics.cpp` | the exit and access violation logs |
| `quick_start.cpp` | the movie-skip flag, the splash-screen skip |
| `boot_trace.cpp` | the start-up log |

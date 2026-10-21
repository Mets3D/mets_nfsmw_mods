# Dev Tools

This mod for Need for Speed: Most Wanted 2005 (1.3) contains some tools that may come in handy during development of other mods.

## Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)

## Diagnostics

`ExitProcess` and `TerminateProcess` (of the game) are logged with the places in the game's code the call came from, and the first three access violations with their registers. (That is how the Direct3D failure of a second full screen copy was found.)

## Quick start

Sets the same flag `NFSMWExtraOptions`'s "Skip Movies" and the widescreen fix's "Skip Intro" use: a byte the game checks before opening any FEng movie (the EA logo, the PSA, the attract movie at startup), posting that movie's "finished" message right away instead of playing it. It is cleared again after the attract movie, so the in-game movies (the event types' "View Tutorial") play. Our plugin sets it a couple of seconds after the game starts, so the first movie sometimes plays anyway; renaming a movie's file (the game then skips it) has no such race.

**Auto load:** set `auto_load_alias=<save name>` under `[quick_start]` in `nfsmw_dev_tools.ini` (independent of `skip_intros=1`, which controls the movie skip). If a save with that name exists, the splash screen ("click to continue") moves on by itself once it has loaded, and that save is loaded straight away, even when there are several (no list to pick from); you land on its "Load complete" message. Empty, or a name with no save: the game starts as usual. The save is looked for where the game looks, so a widescreen fix that keeps user files in the game folder (`CustomUserFilesDirectoryInGameDir`) is followed.

## Free camera

Press **F1** while driving to fly around with a free camera (the game's own debug camera), and again to go back to your car. Your car ignores the keyboard while the camera is on.

The controls use your own keyboard bindings from the Controls menu:

- Mouse: look around.
- Accelerate / brake: move forward / back, in the direction you look.
- Steer left / right: move sideways.
- Shift up / shift down: move up / down.
- Nitro: move faster.
- Speedbreaker: put your car where the camera is (standing still, facing the way the camera faces).

`[free_camera]` in `nfsmw_dev_tools.ini` sets the key, the speed and the mouse sensitivity.

## Ideas / TODOs
- An options menu to tweak the current race's settings, like difficulty level, rubber banding (Catch Up), number of opponents, number of laps, traffic, police presence, and maybe even hidden values like opponent car value, however that is determined? The race would have to be restarted for the changes to take effect.
- Would be interesting to display the perspective of a CPU player in the corner of the screen - Or, maybe it would be much easier, to allow a CPU to take control of one of the split screen players - But how do CPU players work, do they give their cars inputs like a human player, or is it something else?
- Ability to disable collisions on the race arrow walls? Just allow the player to go past, or find some way to detect out of bounds and reset the car after a while?
- A replay system, so we can spectate each other?
- A ghost system, so we can race against our best times?

## AI Disclaimer

*Disclaimer: The code was written by Claude (Sonnet & Opus).*

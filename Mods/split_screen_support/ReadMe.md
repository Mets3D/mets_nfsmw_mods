# Split Screen Support

*Disclaimer: The code was written by Claude (Sonnet & Opus). This ReadMe was written by me, a human.*

This mod for Need for Speed: Most Wanted 2005 (1.3) implements full Split Screen support.

## How to use

1. Install it together with [Controller Support](../controller_support/ReadMe.md): without it, player 2's controller also drives player 1's car.
2. Choose **Quick Race > Split Screen**. Each player presses Start (or Enter) on their own device. It can be a keyboard + controller, or 2 controllers. The first device to press Start/Enter will be on top, the 2nd on bottom.
3. Pick the cars, the track and the options, and race.

## Features
- Each player gets their own minimap and minimap color, not present in the base game. Can be disabled in .ini.
- Each player can fully control their own camera.
- Either player can invoke the pause menu, and change their keybinds during a race.
- Each player's screen renders with its own shadows, reflection cubemap, geometry culling.
- Performance optimization culling smaller objects is disabled, since it's not needed on modern computers. Can be disabled in .ini.

## Mod Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- Required for player 2's input: [Controller Support](../controller_support/ReadMe.md).
- Conditionally compatible: NFSMWExtraOptionsSettings: Set `ExperimentalSplitScreenFix = 0` in `.ini`. If it is on, this mod will do nothing.

## Device Compatibility

- Both using a keyboard is not supported.
- For supported controllers, see the [Controller Support](../controller_support/ReadMe.md) mod - The game natively supports Xbox controllers, and for everything else, Steam Input is tested and works as a translation layer.

## TODO
- Bug: Mouse look doesn't work still.
- Bug?: In at least a sprint (didn't test other race modes), if either player reverses from the starting line, the race will be restarted. Seems unnecessary if this is intended, since the race can be restarted from the pause menu anyways.
- Make install instructions more detailed; Where should users copy the files? Maybe add some CI/CD.

## .ini settings

All in `scripts/mets_mods/nfsmw_split_screen_support.ini`, all switches are 1 (on) by default.

| Setting | What |
|---|---|
| `[fixes] scenery_detail` | draws the scenery with the single player detail rule (more small objects, more far detail; a little slower) |
| `[hud] minimap` | each player has a minimap of their own, instead of just 1 minimap that favors player 1 |
| `[hud] minimap_offset` | how far each player's minimap is from the middle line of the screen (default 120) |

<details>

<summary> Developer notes </summary>

## Developer options

These are for finding out which part causes a problem; split screen needs all of them. The mod turns the three fixes off by itself when NFSMWExtraOptions' split screen fix is on.

| Part | What | Switch |
|---|---|---|
| Menu entry | adds **Split Screen** to the Quick Race menu, after Quick Play and Custom Race | `[menu] entry` |
| Split view | switches player 2's view on (the game then loads the world around them, lights and sets up their view) and draws the world once for each player: player 1 on the top half of the screen, player 2 on the bottom half | `[render] split` |
| Shadows | each player's shadow map is picked around that player | `[render] shadow_fix` |
| Players | finds out who pressed Start (a controller, or Enter / Space on the keyboard); that device alone drives that player's car (every device can still use the menus: car select, pause, Controls); the other devices drive no vehicle | `[players] enabled` |
| Shared device | lets player 2 get past the Press Start screen at all (the game sees every press as player 1's device) | `[fixes] shared_device` |
| Loading screen | uses the normal loading screen: the split screen one crashes | `[fixes] loading_screen` |
| View mode | sets the 3D view up for "two views", which the PC renderer leaves off (black screen) | `[fixes] view_mode` |
| State log | writes the player count, the controllers and the view mode to the log | `[log] state` |
| Hooks | `[hooks] <name>=0` skips one hook of a part (names are in the log) | |

## Files

| File | What |
|---|---|
| `main.cpp`, `split_screen_support.h` | the entry: the log, the ini, each part's switch |
| `menu_entry.cpp` | the Quick Race menu entry |
| `fixes.cpp` | the three changes to the game's code (`../common/patch.h` does the patching) |
| `split_view.cpp` | the two halves of the screen |
| `minimap.cpp` | the second minimap and the marker colours |
| `players.cpp` | which controller is whose |
| `state_log.cpp` | the log of the game's split screen state |
| `events.cpp` | the game functions several parts need (a race loading, each frame) |

</detials>
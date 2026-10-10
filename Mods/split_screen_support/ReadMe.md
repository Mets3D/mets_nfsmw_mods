# Split Screen Support

*Disclaimer: The code was written by Claude (Sonnet & Opus). This ReadMe was written by me, a human.*

This mod for Need for Speed: Most Wanted 2005 (1.3) implements full Split Screen support.

## How to use

1. Install it together with [Controller Support](../controller_support/ReadMe.md): without it, player 2's controller also drives player 1's car.
2. Choose **Quick Race > Split Screen**. Each player presses Start (or Enter) on their own device. It can be a keyboard + controller, or 2 controllers.
3. Pick the cars, the track and the options, and race.

## Mod Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- Required for player 2's input: [Controller Support](../controller_support/ReadMe.md).
- Conditionally compatible: NFSMWExtraOptionsSettings: Set `ExperimentalSplitScreenFix = 0` in `.ini`. If it is on, this mod will do nothing.

## Device Compatibility

- Both using a keyboard is not supported.
- For supported controllers, see the [Controller Support](../controller_support/ReadMe.md) mod - The game natively supports Xbox controllers, and for everything else, Steam Input is tested and works as a translation layer.

## TODO
- In Bumper view, the car is not being rendered. This needs to be integrated, such that each of the 2 screens only stops rendering their own car if it is using bumper view. Currently, a player1 using bumper view is invisible on player2's screen, and a player2 using bumper view has their camera clip into their car, since it's still being rendered on their screen.
- Minimap currently is centered around strictly player 1. Would be nice if it either zoomed out to fit both, or if each player had one minimap, or if it switched to whoever is in last place.
- Something is off about the drawing of shadows - In one example, Player 1 (top)'s shadows popped in as Player 2 caught up to them, suggesting that detailed shadows (of trees in this case, maybe the trees themselves?) are only drawn near Player 2, but that would be surprising, so I'm not sure. Maybe it's a two-way street in some way.
- Reflections on player 2's car change based on player 1's position in the world.
- Would be nice if each player had a different color on the minimap.

<details><summary> Claude nonsense </summary>

## .ini config

| Part | What | Switch |
|---|---|---|
| Menu entry | adds **Split Screen** to the Quick Race menu, after Quick Play and Custom Race | `[menu] entry` |
| Split view | switches player 2's view on (the game then loads the world around them, lights and sets up their view) and draws the world once for each player: player 1 on the top half of the screen, player 2 on the bottom half | `[render] split` |
| Players | finds out who pressed Start (a controller, or Enter / Space on the keyboard); that device alone drives that player's car (every device can still use the menus: car select, pause, Controls); the other devices drive no vehicle | `[players] enabled` |
| Shared device | lets player 2 get past the Press Start screen at all (the game sees every press as player 1's device) | `[fixes] shared_device` |
| Loading screen | uses the normal loading screen: the split screen one crashes | `[fixes] loading_screen` |
| View mode | sets the 3D view up for "two views", which the PC renderer leaves off (black screen) | `[fixes] view_mode` |
| State log | writes the player count, the controllers and the view mode to the log | `[log] state` |

All switches are 1 (on) by default, in `scripts/mets_mods/nfsmw_split_screen_support.ini`.

## Files

| File | What |
|---|---|
| `main.cpp`, `split_screen_support.h` | the entry: the log, the ini, each part's switch |
| `menu_entry.cpp` | the Quick Race menu entry |
| `fixes.cpp` | the three changes to the game's code (`../common/patch.h` does the patching) |
| `split_view.cpp` | the two halves of the screen |
| `players.cpp` | which controller is whose |
| `state_log.cpp` | the log of the game's split screen state |
| `events.cpp` | the game functions several parts need (a race loading, each frame) |

</detials>
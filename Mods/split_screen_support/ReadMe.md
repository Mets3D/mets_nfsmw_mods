# Split Screen Support

*Disclaimer: The code was written by Claude (Sonnet & Opus).*

A plugin (`nfsmw_split_screen_support.asi`) for Need for Speed: Most Wanted (2005, PC, `speed.exe` 1.3) that tries to make the game's two player split screen mode reachable and playable. **Experimental: work in progress.**

The console versions of the game have **Quick Race > Split Screen**. The PC version still contains much of it (the icon, the text, the two player screens, a camera for player 2, the code that handles the choice), but its Quick Race menu is built without the entry, its renderer draws one view, and its input has one device for player 1 that merges the keyboard and every controller.

## TODO
- Currently it seems only one player can open the pause menu, and only the keyboard + one controller's controls can be customized.
- The same controller can press Start to add both players - though the result still works, I think this is a bit strange. I might expect that when the same player presses Start after having already picked their car, they might be able to re-pick their car, for example.
- Minimap currently favors player 1. Would be nice if it either zoomed out to fit both, or if each player had one minimap.
- Something is off about the drawing of shadows - In one example, Player 1 (top)'s shadows popped in as Player 2 caught up to them, suggesting that detailed shadows (of trees in this case, maybe the trees themselves?) are only drawn near Player 2, but that would be surprising, so I'm not sure. Maybe it's a two-way street in some way.
- Reflections on player 2's car change based on player 1's position in the world.
- Would be nice if each player had a different color on the minimap.

## What it does

| Part | What | Switch |
|---|---|---|
| Menu entry | adds **Split Screen** to the Quick Race menu, after Quick Play and Custom Race | `[menu] entry` |
| Split view | switches player 2's view on (the game then loads the world around them, lights and sets up their view) and draws the world once for each player: player 1 on the top half of the screen, player 2 on the bottom half | `[render] split`, `layout`, `band_center` |
| Players | finds out who pressed Start (a controller, or Enter / Space on the keyboard); that device alone controls that player's menus (their car select) and car; the other devices are ignored | `[players] enabled` |
| Player 2 input | drives player 2's car from player 2's controller or from the keyboard | `[player2] enabled`, buttons and keys |
| Shared device | lets player 2 get past the Press Start screen at all (the game sees every press as player 1's device) | `[fixes] shared_device` |
| Loading screen | uses the normal loading screen: the split screen one crashes | `[fixes] loading_screen` |
| View mode | sets the 3D view up for "two views", which the PC renderer leaves off (black screen) | `[fixes] view_mode` |
| State log | writes the player count, the controllers and the view mode to the log | `[log] state` |

All switches are 1 (on) by default, in `scripts/mets_mods/nfsmw_split_screen_support.ini`.

**Who plays with what**: on each "Press Start" screen, the device that pressed Start (a controller's Start, or Enter or Space) is the one that plays that player, in the menus that follow (the car select) and in the race. Every other device is ignored for that player, even a connected controller when the keyboard pressed Enter, and the other way round. If the same device presses Start for both players, or one of them does not, the second player gets another device: a free controller, else the keyboard. With [Controller Support](../controller_support/ReadMe.md) installed the keyboard and any number of controllers can be used; without it only the game's own controller can.

**Player 2's controller**: RT gas, LT brake and reverse, left stick steering, B handbrake, A nitrous, Y look back, RB / LB shift up / down. The buttons can be changed in the ini:

```ini
[player2]
handbrake=B      ; A B X Y LB RB LS RS Back, or none
nitrous=A
look_back=Y
shift_up=RB
shift_down=LB
```

**Player 2 on the keyboard** (player 1 on a controller; the keyboard is then all player 2's): arrow keys, Space handbrake, Left Alt nitrous, L look back, Left Shift / Left Ctrl shift up / down. Player 1 on the keyboard uses the game's own keys, from the Controls menu.

```ini
[player2_keys]
steer_left=Left   ; A-Z, 0-9, Up Down Left Right, Space, Enter, Tab, LShift RShift, LCtrl RCtrl, LAlt RAlt,
steer_right=Right ; or a DirectInput key code as a number
gas=Up
brake=Down
handbrake=Space
nitrous=LAlt
look_back=L
shift_up=LShift
shift_down=LCtrl
```

**The picture**: each half shows the middle band of a full-screen picture, so nothing is stretched, but less is seen above and below. `band_center` (0.25 to 0.75, default 0.55) moves the band down to keep more of the car in it; `layout=squash` shows the whole picture squeezed to half its height instead.

```ini
[render]
layout=crop       ; or squash
band_center=0.55
```

## Trying it

1. Install it together with [Controller Support](../controller_support/ReadMe.md): without it, player 2's controller also drives player 1's car.
2. Turn **motion blur off** in the video options: it blends the previous frame in, which is now the other player's.
3. Choose **Quick Race > Split Screen**. Each player presses Start (or Enter) on their own device.
4. Pick the cars, the track and the options, and race.
5. Send `scripts/mets_mods/nfsmw_split_screen_support.log` (and `nfsmw_controller_support.log`) when something goes wrong.

Not done yet: the HUD is player 1's, drawn over the whole screen; only player 1 can pause; the shadows and reflections of the second view; whatever the race itself does wrong with two players. Player 2 on a controller has their own rumble and, with the right stick, their own camera look (both from [Controller Support](../controller_support/ReadMe.md)); on the keyboard they have no stick, but the mouse looks around in their camera (as it does for player 1 in single player). After the race, every device controls the menus again.

## Where it comes from

The loading screen and shared device fixes, and the idea of the menu entry, are from the `ExperimentalSplitScreenFix` setting of [NFSMWExtraOptions](https://github.com/ExOptsTeam/NFSMWExOpts) (its source is GPL-3.0). They were re-read in the game's code and rewritten here; its third patch (keeping the 3D views from being switched off, in every game) is replaced by the view mode fix. The split view, the players and player 2's input are new.

With this plugin, leave `ExperimentalSplitScreenFix = 0` in `NFSMWExtraOptionsSettings.ini`. If it is on, the plugin finds the game's code already changed, says so in the log and leaves those parts to it.

## Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- Recommended: [Controller Support](../controller_support/ReadMe.md), so that each controller drives only its own car.

## Files

| File | What |
|---|---|
| `main.cpp`, `split_screen_support.h` | the entry: the log, the ini, each part's switch |
| `menu_entry.cpp` | the Quick Race menu entry |
| `fixes.cpp` | the three changes to the game's code (`../common/patch.h` does the patching) |
| `split_view.cpp` | the two halves of the screen |
| `players.cpp` | which controller is whose |
| `player2_input.cpp` | player 2's car |
| `state_log.cpp` | the log of the game's split screen state |
| `events.cpp` | the game functions several parts need (a race loading, each frame) |

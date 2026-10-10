# Split Screen Support

*Disclaimer: The code was written by Claude (Sonnet & Opus).*

A plugin (`nfsmw_split_screen_support.asi`) for Need for Speed: Most Wanted (2005, PC, `speed.exe` 1.3) that tries to make the game's two player split screen mode reachable and playable. **Experimental: work in progress.**

The console versions of the game have **Quick Race > Split Screen**. The PC version still contains much of it (the icon, the text, the two player screens, a camera for player 2, the code that handles the choice), but its Quick Race menu is built without the entry, its renderer draws one view, and its input has one device for player 1 that merges the keyboard and every controller.

## TODO
- Claude warns that Motion Blur won't work properly. Test this, and if it is true, see if we can disable Motion Blur whenever in split screen mode.
- Keyboard player is currently not able to be added as a player, though they can control the first controller's car. Would be nice if they could be added as a unique player, and even nicer if they were a different player than the first controller player. That way, two players could play against each other, one of them on keyboard, the other on controller.
- Currently it seems only one player can open the pause menu, and only one player's controls can be customized.
- What happens if the same controller presses Start to add both players?
- The camera mod's camera rotation currently rotates the camera for both players.

## What it does

| Part | What | Switch |
|---|---|---|
| Menu entry | adds **Split Screen** to the Quick Race menu, after Quick Play and Custom Race | `[menu] entry` |
| Split view | draws the world once for each player: player 1 on the top half of the screen, player 2 on the bottom half | `[render] split`, `layout`, `band_center` |
| Streaming | loads the world around player 2 as well as around player 1 | `[streaming] player2` |
| Players | lets either controller press Start, finds out whose it was, then gives each player's screens (car select) to their own controller; in the race only player 1's controller (or the keyboard) reaches the game | `[players] enabled` |
| Player 2 input | drives player 2's car from player 2's controller | `[player2] enabled`, buttons |
| Shared device | lets player 2 get past the Press Start screen at all (the game sees every press as player 1's device) | `[fixes] shared_device` |
| Loading screen | uses the normal loading screen: the split screen one crashes | `[fixes] loading_screen` |
| View mode | sets the 3D view up for "two views", which the PC renderer leaves off (black screen) | `[fixes] view_mode` |
| State log | writes the player count, the controllers and the view mode to the log | `[log] state` |

All switches are 1 (on) by default, in `scripts/mets_mods/nfsmw_split_screen_support.ini`.

**Player 2's controller**: RT gas, LT brake and reverse, left stick steering, B handbrake, A nitrous, Y look back, RB / LB shift up / down. The buttons can be changed in the ini:

```ini
[player2]
handbrake=B      ; A B X Y LB RB LS RS Back, or none
nitrous=A
look_back=Y
shift_up=RB
shift_down=LB
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
3. Choose **Quick Race > Split Screen**. Each player presses Start on their own controller (player 1 may use Enter on the keyboard instead).
4. Pick the cars, the track and the options, and race.
5. Send `scripts/mets_mods/nfsmw_split_screen_support.log` (and `nfsmw_controller_support.log`) when something goes wrong.

Not done yet: the HUD is player 1's, drawn over the whole screen; player 2 cannot use the keyboard; player 2's car does not rumble; whatever the race itself does wrong with two players.

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
| `streaming.cpp` | the world loaded around player 2 |
| `players.cpp` | which controller is whose |
| `player2_input.cpp` | player 2's car |
| `state_log.cpp` | the log of the game's split screen state |
| `events.cpp` | the game functions several parts need (a race loading, each frame) |

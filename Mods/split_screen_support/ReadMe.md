# Split Screen Support

This mod for Need for Speed: Most Wanted 2005 (1.3) implements full Split Screen support.

## How to use

1. Install it together with [Controller Support](../controller_support/ReadMe.md): without it, player 2's controller also drives player 1's car.
2. Choose **Quick Race > Split Screen**. Each player presses Start (or Enter) on their own device. It can be a keyboard + controller, or 2 controllers. The first device to press Start/Enter will be on top, the 2nd on bottom.
3. Pick the cars, the track and the options, and race.

## Features
- Each player gets their own minimap and minimap color, not present in the base game. Can be turned off in the [.ini](nfsmw_split_screen_support.ini) (`use_split_minimap`).
- Each player can fully control their own camera.
- Either player can invoke the pause menu, and change their keybinds during a race.
- Each player's screen renders with its own shadows, reflection cubemap, geometry culling.
- Performance optimization culling smaller objects is disabled, since it's not needed on modern computers. Can be turned back on in the [.ini](nfsmw_split_screen_support.ini) (`cull_small_objects`).

## Mod Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- Required for player 2's input: [Controller Support](../controller_support/ReadMe.md).
- Confirmed compatible: 
    - WideScreenFix.
    - My other mods.
- Conditionally compatible: NFSMWExtraOptionsSettings: Set `ExperimentalSplitScreenFix = 0` in its `.ini`.

## Device Compatibility

- Both using a keyboard is not supported.
- For supported controllers, see the [Controller Support](../controller_support/ReadMe.md) mod - The game natively supports Xbox controllers, and for everything else, Steam Input is tested and works as a translation layer.

## TODO
- Bug: Seems like with 2 controllers, one of the controllers isn't allowed to be Player 1?
- Would be cool to add a Minimap option under Options -> Player, that can actually be toggled separately for Player 1 and 2, so each player can decide if they want a minimap, which matches with the other options already in this menu.
- Bug?: I think the camera values are based on player1's car, resulting in player2's hood view clipping into the car (player 1 with the Corvette, player 2 with the BMW)
- Bug?: In at least a sprint (didn't test other race modes), if either player reverses from the starting line, the race will be restarted. Seems unnecessary if this is intended, since the race can be restarted from the pause menu anyways.
- Make install instructions more detailed.

## Files and settings

In the folder `scripts/mets_mods/` (next to the `.asi` files):
- [`nfsmw_split_screen_support.ini`](nfsmw_split_screen_support.ini) to configure the mod.
- `nfsmw_split_screen_support.log`: Debug logging from the mod.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

# Split Screen Support

This mod for Need for Speed: Most Wanted 2005 (1.3) implements full Split Screen support.

## How to use

1. Installing [Controller Support](../controller_support/ReadMe.md) mod highly recommended: without it, you can't have both players playing on a controller, one must use the keyboard.
2. Choose **Quick Race > Split Screen**. Each player presses Start (or Enter) on their own device. It can be a keyboard + controller, or 2 controllers. The first device to press Start/Enter will be on top, the 2nd on bottom.
3. Pick the cars, the track and the options, and race.

## Features
- Each player gets their own minimap and minimap color, not present in the base game. Can be turned off in the [.ini](nfsmw_split_screen_support.ini) (`use_split_minimap`).
- Each player can fully control their own camera.
- Either player can invoke the pause menu, and change their keybinds during a race.
- Each player's screen renders with its own shadows, reflection cubemap, geometry culling.
- Performance optimization culling smaller objects is disabled, since it's not needed on modern computers. Can be turned back on in the [.ini](nfsmw_split_screen_support.ini) (`cull_small_objects`).

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Required for both players using a controller: [Controller Support](../controller_support/ReadMe.md).
- Conditionally compatible: NFSMWExtraOptionsSettings: Set `ExperimentalSplitScreenFix = 0` in its `.ini`.

## Device Compatibility

- Both players using a keyboard is not supported.
- For supported controllers, see the [Controller Support](../controller_support/ReadMe.md) mod - The game natively supports Xbox controllers, and for everything else, Steam Input is tested and works as a translation layer.

## TODO
- Each minimap should be toggle-able per player, Options -> Player.
- Bug?: I think the camera values are based on player1's car, resulting in player2's hood view clipping into the car (player 1 with the Corvette, player 2 with the BMW)
- Bug?: In at least a sprint (didn't test other race modes), if either player reverses from the starting line, the race will be restarted. Seems unnecessary if this is intended, since the race can be restarted from the pause menu anyways.
- CPU racers in split screen races?
- Allow AI to control one of, or even both the players? Maybe an option that Dev Tools adds in the Player menu? I believe this is already implemented in some mod on an esoteric hotkey for singleplayer.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

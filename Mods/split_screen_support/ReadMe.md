# Split Screen Support

This mod for Need for Speed: Most Wanted 2005 (1.3) implements full Split Screen support.

## How to use

1. Installing [Controller Support](../controller_support/ReadMe.md) mod highly recommended: without it, you can't have both players playing on a controller, one must use the keyboard.
2. Choose **Quick Race > Split Screen**. Each player presses Start (or Enter) on their own device. It can be a keyboard + controller, or 2 controllers. The first device to press Start/Enter will be on top, the 2nd on bottom.
3. Pick the cars, the track and the options, and race.

## Features
- Each player gets their own minimap and minimap color, not present in the base game. With [Custom Menus](../custom_menus/ReadMe.md) installed, each player can hide their own minimap in **Options > Player > Minimap**. Can be turned off in the [.ini](nfsmw_split_screen_support.ini) (`use_split_minimap`).
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
- The sun seems to be drawn for player 2 depending on player 1's perspective? Specifically, when player 1 has the sun in their view, player 2 might also get a sun rendered kinda arbitrarily in their view.
- The player 1 and player 2 devices are locked in for the entire playsession after first starting a split screen race. This should not be the case, they should reset whenever we enter the split screen menu where it says "Player 1 press start" or so. Currently after 2 devices do a split-screen race, a 3rd device can't participate until the game is restarted (afaik).
- Would be nice if we remembered the selected car for each player across sessions.
- Would be nice if after a race we didn't get kicked all the way out and then having to click through all the menus again. The menu flow could use re-working.
- How about vertical split screen? ;)
- How about 4-player split screen?? :')

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

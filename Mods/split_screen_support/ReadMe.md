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
- Would be nice if after a race we didn't get kicked all the way out to the Quick Race menu, and then having to click through all the menus again.
- How about vertical split screen? ;)
- 3 or 4-player split screen (work in progress, `more_players=1` in the [.ini](nfsmw_split_screen_support.ini), needs [Custom Menus](../custom_menus/ReadMe.md)): the menus are done (a **Players** row, 2 or 4, in the race options, and a second round of Press Start and Car Select for players 3 and 4), but only players 1 and 2 are in the race. Players 3 and 4 get cars that stand still on the grid, and the 3D world is drawn in four quadrants (a simple chase camera for each of the new cars). Their views also get the track streamed and the car reflections around them. Missing: input for them, the HUD and minimaps for 4 views, their own camera settings, a bigger car memory pool for four cars, and 3 players.
- Port the options from Quick Race to Split Screen: NPC opponents, Difficulty, Catch-Up, Heat Level.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

# Split Screen Support

This mod for Need for Speed: Most Wanted 2005 (1.3) implements full Split Screen support.

## How to use

1. Installing [Controller Support](../controller_support/ReadMe.md) mod highly recommended: without it, you can't have both players playing on a controller, one must use the keyboard.
2. Choose **Quick Race > Split Screen**. Each player presses Start (or Enter) on their own device. It can be a keyboard + controller, or 2 controllers. The first device to press Start/Enter will be on top, the 2nd on bottom.
3. Pick the cars, the track and the options, and race.

## Features
- Each player gets their own minimap and minimap color, not present in the base game. With [Custom Menus](../custom_menus/ReadMe.md) installed, each player can hide their own minimap in **Options > Player > Minimap**.
- With [Custom Menus](../custom_menus/ReadMe.md) installed, each player can scale their HUD (also in a game with one player) in **Options > Player > HUD Scale** (40% to 100%): each part of the HUD (race info at the top right, speedometer at the bottom right, minimap at the bottom left) shrinks around its own corner of the player's part of the screen.
- Each player can fully control their own camera.
- The Car Select screen says which player it is for and with which device ("Player 2 - Xbox One S Controller", the name the Controls menu shows).
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

## 3 and 4-player splitscreen (work in progress: `more_players=1` in the [.ini](nfsmw_split_screen_support.ini))
Needs [Custom Menus](../custom_menus/ReadMe.md) (the **Players** row, 2, 3 or 4, in the options of a split screen race) and [Controller Support](../controller_support/ReadMe.md) (an input device each). With 3 or 4 players there is a second round of Press Start and Car Select for players 3 and 4 (on a screen of Press Start, **F9** gives the player the next device nobody has yet, for testing with fewer devices than players), a 2x2 starting grid, and the screen in four quadrants (with 3 players, player 1 has the top half, and a HUD of full size; players 2 and 3 have a quadrant each): each player has their own camera (Change Camera View, look, look back), the track streamed around them, reflections, a HUD with a minimap (every minimap shows every other player, each player has their own marker colour), the 3, 2, 1, Go!, and their own **Options > Player > Minimap** and **HUD Scale** (the HUD of a quadrant is half the size of the HUD of a half screen at 100%). The pause menu is the one of the player who pressed pause.
Not done yet:
- Rumble for players 3 and 4 is built (each on their own controller) but not confirmed on a real controller yet. Their camera views are rough copies of the game's (the game has no camera for them). No camera settings for them.
- 5 to 9 players (the layout code is for any number, the rest is for two extra players).

## Older TODOs, not 4-player split-screen specific
- The sun seems to be drawn for player 2 depending on player 1's perspective? Specifically, when player 1 has the sun in their view, player 2 might also get a sun rendered kinda arbitrarily in their view.
- The player 1 and player 2 devices are locked in for the entire playsession after first starting a split screen race. This should not be the case, they should reset whenever we enter the split screen menu where it says "Player 1 press start" or so. Currently after 2 devices do a split-screen race, a 3rd device can't participate until the game is restarted (afaik).
- Would be nice if we remembered the selected car for each player across sessions.
- Would be nice if after a race we didn't get kicked all the way out to the Quick Race menu, and then having to click through all the menus again.
- How about vertical split screen? ;)
- I believe we are still rendering each screen at full resolution and then scaling it down. I assume it would be a significant performance boost if we just rendered the view of each player at its final size to begin with instead of this silliness.
- Port the options from Quick Race to Split Screen: NPC opponents, Difficulty, Catch-Up, Heat Level.
- Maybe we could also share shadow maps when players are close to each other, but I wouldn't bother with this yet.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

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

## 4-player splitscreen TODO
- Minimap bug: Player 1 & 2's minimap shows players 1 & 2 only. Player 3 & 4's minimap shows only themself, and Player 1. So all minimaps are missing 2 players with various logics.
- The mod already adds a per-player "Minimap: On/Off" option to the Player menu. First of all, this has an issue: Player 1 toggles their own Minimap, Player 2 toggles everyone else's minimap. It should be per-player. The Player menu displays options for whichever player's input device opened the pause menu before entering the Player options menu.
- Camera controls also seem wrong. Player 1's "Next/Previous camera" inputs work correctly, Player 2 as well. Player 3's however, seems to affect Player 1 only. Camera Look also isn't working for player 3 (and player 4 I can't test remember, but presumably identical issues.) Of course, some of these issues may require changes in other mods, such as Controller Support, or even Custom Menus.
- I'd like to add a "HUD Scale" option, which scales each HUD corner: The minimap in the bottom-left, the race info in the top-right (which is currently incorrectly placed in the center-right), and the speedo in the bottom-right.
- I'm getting the impression that currently the right-side UI is all one group of elements, that's probably why fixing the issue where the speedo was in the center-right, just resulted in the race info ending up in the center-right. We'd ideally need to be able to control those two UI areas independently, ie. we need control over the UI independently for each corner, like I described. AFAIK there's no HUD in the top-left, but if I'm wrong, include that in this as well of course.
- I think it might also be cool if instead of "Player 1/2/3/4" in the car select menu, it would also display the input device's name (same that's displayed in the Controls menu). Is this possible?

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

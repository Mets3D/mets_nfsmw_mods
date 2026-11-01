# Split Screen Support

This mod for Need for Speed: Most Wanted 2005 (1.3) implements full Split Screen support.

## How to use

1. Installing [Controller Support](../controller_support/ReadMe.md) mod highly recommended: without it, you can't have both players playing on a controller, one must use the keyboard.
2. Choose **Quick Race > Split Screen**. Each player presses Start (or Enter) on their own device. It can be a keyboard + controller, or 2 controllers. The first device to press Start/Enter will be on top, the 2nd on bottom.
3. Pick the cars, the track and the options, and race.

## Features
Points marked with * require the [Custom Menus](../custom_menus/ReadMe.md) mod to be installed. They're bundled together in the release.
- 2, 3, and 4 player split-screen.
- AI opponents allowed: Up to 16 total cars in a race.
- Each player can invoke the pause menu. The "Player Options" menu will affect the player who opened the pause menu. The name of the affected player is displayed in the menu's header.
- *Each player gets their own minimap and minimap color. Each player can hide their own minimap in **Options > Player > Minimap**.
- *Each player can scale their HUD (also in a game with one player) in **Options > Player > HUD Scale**.
- *Each player can set their own volume in **Options > Player > Split-Screen Volume**.
- Tollbooth races are supported: Each player has their own clock, if you run out of time you get knocked out, starting positions work for up to 16 cars (although above 4 it gets unfair), and the end-of-race summary screen shows your remaining time.
- If a player is eliminated, their screen disappears, one of the neighbouring players takes up the available screen space. The eliminated player's resources are freed, their screen no longer rendered, their audio muted.

## Non-features
This is a bit of a documentation on what issues were encountered during development. Things you should take for granted, but other mods that aren't paying enough attention might miss.
- World Geometry, shadows, reflections, light trails, sparks, and sun glare, are rendered correctly for each player. The sun was pain in the butt.
- The game now allocates enough memory and skin slots for cars to allow 16 unique racers, and also for 4 sets of minimap tiles.
- The engine sounds of all AI opponents plays - albeit the computationally cheap versions.
- Sound effects for all players plays correctly. This was a surprisingly huge challenge.
- Each player can choose from player 1's cars, including those of their Career and their custom cars.
- Each player can control their own camera - which camera preset they're on, looking around with free look from the Controller Support mod, and look-back.
- The mouse looks around only for the player using the keyboard, if any.
- Controller rumble works for all 4 players.
- The camera is offset by a hard-coded amount for players whose screens are wider due to the split-screen layout: Player 1 in 3-player, and both players in 2-player mode.
- Performance optimization culling smaller objects is disabled, since it's not needed on modern computers. Can be turned back on in the [.ini](nfsmw_split_screen_support.ini) (`cull_small_objects`).
- After a race, the game goes to the race options instead of all the way out into the main menu.
- After a race, the previously selected cars are remembered, even across sessions, and the menu to select cars again is optional, unless the player count changes. Whichever player presses the "Select Car" button, gets to change their car.
- The Car Select screen and the Player Options menus show which player they're changing. ("Player 2 - Xbox One S Controller").
- Races start with the intro cutscenes just like in single-player.
- All 16 cars spawn in correct places, whether the race has a 2-row (2x2) or a 1-row (1x4) layout.
- Knockout races work: You cannot specify lap count, it is determined by total racers. When you get knocked out, you no longer control your car, but you can still open the pause menu.
- All opponents including AI opponents appear on the minimap and also the world map.

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Required for both players using a controller: [Controller Support](../controller_support/ReadMe.md).
- Conditionally compatible: NFSMWExtraOptionsSettings: Set `ExperimentalSplitScreenFix = 0` in its `.ini`.

## Device Compatibility

- Two players using a keyboard is not supported.
- For supported controllers, see the [Controller Support](../controller_support/ReadMe.md) mod - The game natively supports Xbox controllers, and for everything else, Steam Input is tested and works as a translation layer.

---

# TODOS
- The only issue with Speedtrap is that you can't see your standing in the race - I believe the leaderboard HUD is just always disabled in all split-screen modes. I think this is unnecessary because there is a per-player option to disable it anyways.
- Tollbooth added option idea: "Time Reduction", a percentage value to tweak the difficulty to one's liking.
- I think if we skip the intro cinematic, the cars end up starting the race in whatever position they happened to be in in the moment of the skip. Not good!
- I think mirrors are disabled in split-screen, I wonder why, and if we could lift it, and how bad the performance impact would be.
- Minor UI issue: Values of race options turn gray after stepping over them.
- Would be fun to allow customizing some minimap properties: Player 1,2,3,4 colors, AI opponent color, AI opponent size.
- Idea: Option to change race start layout between the current behaviour (humans first), vs humans in the back, vs randomized.
- How about vertical split screen? ;) Maybe an option under Gameplay. In 3-player, player 1 would take the entire left side, while players 2 and 3 share the right side.
- Heat Level (cops) is the one Quick Race option not available in Split Screen yet (the others are above, under "Needs your testing"). It is not a row of the game: Redux v3's NFSMWRaceFixes adds it in its own code, so it needs that plugin's row class.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

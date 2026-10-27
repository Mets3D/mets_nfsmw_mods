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
Needs [Custom Menus](../custom_menus/ReadMe.md) (the **Players** row, 2, 3 or 4, in the options of a split screen race) and [Controller Support](../controller_support/ReadMe.md) (an input device each). With 3 or 4 players there is a second round of Press Start and Car Select for players 3 and 4 (on a screen of Press Start, **F9** gives the player the next device nobody has yet, for testing with fewer devices than players), a 2x2 starting grid, and the screen in four quadrants (with 3 players, player 1 has the top half, and a HUD of full size; players 2 and 3 have a quadrant each): each player has their own camera, the game's own (Change Camera View, look, look back, and every setting of [Camera Settings](../camera_settings/ReadMe.md)), the track streamed around them, reflections, a HUD with a minimap (every minimap shows every other player, each player has their own marker colour), the 3, 2, 1, Go!, and their own **Options > Player > Minimap** and **HUD Scale** (the HUD of a quadrant is half the size of the HUD of a half screen at 100%). The pause menu is the one of the player who pressed pause.


## 3 or more player split screen TODO:
- Make clear in the pause menu whose **Options > Player** it is (the player who opened the pause menu): the page's subtitle says "Player 1" whoever that is. At least the subtitle could be dynamic and show the player's name ("Player 3 - Xbox One S Controller", as the Car Select screen does).
- Options > AI Driving (Improved AI Opponents) only has players 1 and 2: players 3 and 4 get player 2's.
- With 3 players, player 3's view is flickering: Every other frame seems to be a slightly different perspective, perhaps different FoV. According to Claude, "Their camera views are rough copies of the game's (the game has no camera for them). No camera settings for them." - This situation needs to improve. I didn't test with 4 players yet.
- 5 to 9 players (the layout code is for any number, the rest is for two extra players).

## To check by hand (built and run by an AI through logs and screenshots only; not seen by a person)
- Players 3 and 4 now have the game's own camera instead of a copy of ours (see the flicker point above): does the flicker of player 3's view still happen, with 3 and with 4 players, while standing still and while driving?
- Camera Settings (**Options > Camera**: FoV, Speed FoV, height, distance, pitch, smoothing, acceleration, tilt) apply to players 3 and 4 as to 1 and 2. (A test with FoV at its smallest changed all the views.)
- Look (right stick, mouse), look back and Change Camera View of players 3 and 4 on real controllers. The mouse should only look around for the player who drives with the keyboard.
- Rumble of players 3 and 4.
- HUD Scale in games of 1, 2, 3 and 4 players, and the HUD of the first player with 3 players (full size).
- Leaving a 3 or 4 player race to the menu and starting another one (the cameras of players 3 and 4 are made and removed with the race: a quit from the pause menu was tested to get back to the menu).
- **Options > Player** of players 3 and 4 (pause menu opened by their own device): Camera, Gauges, Race Information, Split Time, Score, Leaderboard, Minimap, HUD Scale and Free Look Reset should change only that player's. Minimap, HUD Scale and Free Look Reset are kept between sessions (`[player_3]`, `[player_4]` in the .ini of this mod, `[look_player_3]` in the one of Controller Support); the game's own toggles and camera view are not: players 3 and 4 start each race with a copy of player 1's and 2's.

## Older TODOs, not 4-player split-screen specific
- The sun seems to be drawn for player 2 depending on player 1's perspective? Specifically, when player 1 has the sun in their view, player 2 might also get a sun rendered kinda arbitrarily in their view.
    - Also, in 3-player split screen, the sun was even observed to be drawn twice for player 3. When player 1's camera turned away from the sun, the wrong sun on player 3's screen got dimmer, and when player 2's camera also turned away from the sun, the wrong sun on player 3's screen fully disappeared.
- The player 1 and player 2 devices are locked in for the entire playsession after first starting a split screen race. This should not be the case, they should reset whenever we enter the split screen menu where it says "Player 1 press start" or so. Currently after 2 devices do a split-screen race, a 3rd device can't participate until the game is restarted (afaik).
- Would be nice if we remembered the selected car for each player across sessions.
- Would be nice if after a race we didn't get kicked all the way out to the Quick Race menu, and then having to click through all the menus again.
- How about vertical split screen? ;)
- I believe we are still rendering each screen at full resolution and then scaling it down. I assume it would be a significant performance boost if we just rendered the view of each player at its final size to begin with instead of this silliness.
- Port the options from Quick Race to Split Screen: NPC opponents, Difficulty, Catch-Up, Heat Level.
- Maybe we could also share shadow maps when players are close to each other, but I wouldn't bother with this yet.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

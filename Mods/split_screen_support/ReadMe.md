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
- With 3 players, player 3's view is flickering: Every other frame seems to be a slightly different perspective, perhaps different FoV. According to Claude, "Their camera views are rough copies of the game's (the game has no camera for them). No camera settings for them." - This situation needs to improve. I didn't test with 4 players yet.
- More than 4 players: the code is sized by one number, `kMaxPlayers` (8, `Mods/common/max_players.h`), and the **Players** row goes up to `max_players` of the [.ini](nfsmw_split_screen_support.ini) (4 by default). A race with 5 players was run (three rounds of Press Start and Car Select, five cameras and HUDs). Left: the picture of 5 to 8 players is a 3 x 3 grid that leaves the bottom third of the screen black (rows x columns for the count, with cells of another shape than the screen's, would use it all); there are 5 devices (4 controllers and the keyboard) and a device is one player; the game's track streamer keeps the track around two places only, so cars far apart pop in (the more players, the more often); 6 or more players and 4 players on one screen have not been looked at for what they cost to draw.

## To check by hand (built and run by an AI through logs and screenshots only; not seen by a person)
- Players 3 and 4 now have the game's own camera instead of a copy of ours (see the flicker point above): does the flicker of player 3's view still happen, with 3 and with 4 players, while standing still and while driving?
- Camera Settings (**Options > Camera**: FoV, Speed FoV, height, distance, pitch, smoothing, acceleration, tilt) apply to players 3 and 4 as to 1 and 2. (A test with FoV at its smallest changed all the views.)
- Look (right stick, mouse), look back and Change Camera View of players 3 and 4 on real controllers. The mouse should only look around for the player who drives with the keyboard.
- Rumble of players 3 and 4.
- HUD Scale in games of 1, 2, 3 and 4 players, and the HUD of the first player with 3 players (full size).
- The subtitle of **Options > Player** in the pause menu says which player's it is ("Player 3 - Xbox One S Controller"); it said "Player 1" whoever opened it.
- Players 3 and 4 driven by the Improved AI (**Options > AI Driving**, or `control=improved` in `[player_3]` of the Improved AI's .ini): the cars drive off by themselves.
- Leaving a 3 or 4 player race to the menu and starting another one (the cameras of players 3 and 4 are made and removed with the race: a quit from the pause menu was tested to get back to the menu).
- **Options > Player** of players 3 and 4 (pause menu opened by their own device): Camera, Gauges, Race Information, Split Time, Score, Leaderboard, Minimap, HUD Scale and Free Look Reset should change only that player's. Players 3 and 4 keep their choices between sessions in this mod's .ini (`[player_3]`, `[player_4]`: minimap, hud_scale and `settings`, the game's own toggles and camera view as bytes, since the game's profile has room for two players only), and the Free Look Reset in the one of Controller Support (`[look_player_3]`). Until they choose, players 3 and 4 start with a copy of player 1's and 2's settings.

## To check by hand, from the older TODOs
- **The sun / glows** (was: player 2's sun depended on player 1's perspective; in 3 players player 3 got the sun twice, dimming when players 1 and 2 turned away). Likely cause, now changed but not seen by a person: the game draws the lights' glow (flares, headlight and traffic light glows) in a view of its own (view 4) that shares player 1's camera, so every player's picture got glows seen from player 1's place. It now follows the camera of the player being drawn. If a sun that does not belong is still there, say on which track and where the sun is.
- **Devices of the last game** (was: after a split screen game the devices of players 1 and 2 stayed theirs until the game was restarted): the first Press Start screen of a new game frees them. Tested: a second game with controllers 1 and 2 after one with the keyboard and controller 0.
- **After a race** (was: kicked to the Quick Race menu): the game now goes to the race's own options screen (Track_Options.fng: the track, laps and the Players row stay as they were), after the results and after Quit Race. `after_race=track_select` or `after_race=menu` (the game's own) in the [.ini](nfsmw_split_screen_support.ini) change that.

## Older TODOs, not 4-player split-screen specific
- Would be nice if we remembered the selected car for each player across sessions.
- How about vertical split screen? ;)
- Four players run at about 53 frames per second here and that is the CPU: each player's picture costs the game 4 ms of CPU however many pixels it has. Drawing each view at the size of its cell was tried (the game's render target pointed at surfaces of the cell's size for the pass) and gave nothing (53.8 frames per second before, 52.1 after, 4 players, 1920 x 1080), so it was not kept. Not drawing the shadow map, the road reflection and the car reflection at all would give 8.6% (53.4 to 58.0), so the draw calls are not the main cost either. What would help is less work for each picture (see [agent_docs/split-screen.md](../../agent_docs/split-screen.md), "Where the time of a four player frame goes").
- Port the options from Quick Race to Split Screen: NPC opponents, Difficulty, Catch-Up, Heat Level.
- Maybe we could also share shadow maps when players are close to each other, but I wouldn't bother with this yet.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

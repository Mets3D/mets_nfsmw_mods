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

- Two players using a keyboard is not supported.
- For supported controllers, see the [Controller Support](../controller_support/ReadMe.md) mod - The game natively supports Xbox controllers, and for everything else, Steam Input is tested and works as a translation layer.

---

# TODOS

### Needs your testing
Changes made while you were away, which I could only check through logs and screenshots. Please play them, and delete a line once you have confirmed it (or tell me what is still wrong).
- **Car starting positions** (any race with more than 4 cars): the first four cars stand where the race's intro cutscene leaves them (a 2 by 2 grid in a circuit or a sprint, a row of four in a drag race or a lap knockout), and the cars after them follow the layout of the first four, in rows of the same width 7.5 m behind each other, following the road. In a split screen race the first four are put back where the cutscene left them (the game moves players 1 and 2 to the starting lanes after it) and a Restart Race puts everybody back. Checked in your tests on circuits, sprints and drag races with 7 to 16 cars. Please look for cars that start inside a wall or a barrier, off the road, or overlapping, on tracks with a curve at the start.
- **Streaming** (road triangles missing, car reset when driving into them): I could not reproduce the missing geometry. I fixed what looked wrong: the cars of players 3 and up that the streamer follows had no speed (so no look-ahead of the next zone), and swapping which car a streaming slot follows overwrote the streamer's own state. The streamer still has only two places, so cars far apart (3 groups, more than about a kilometre apart) will still lose track somewhere. If you see it again, tell me where the cars were relative to each other.
- **Quick Race options in Split Screen** (Total Racers, Difficulty, Catch Up in the race options, and the AI opponents themselves): they now show in the options of a split screen race and the opponents race with the players. The Opponents row is replaced by **Total Racers** (the players and the AI cars together, from the number of players up to 16; the number of AI cars is the total minus the players, and the total is raised when you add players). It starts at the players plus 3 and is not shared with the single player Quick Race. Checked with 2 players + 3 opponents and 4 players + 3 opponents (logs, a screenshot); the row itself and 16 racers are not seen yet. Please look at: do the AI cars drive the race properly with the players (laps, finish, race positions), anything odd in the results screen, and with a lot of cars: does the game hold up (car memory, loading). The `[options] ai_in_split_screen=0` setting turns it all off.
- **Intro cutscene in a split screen race** (works in your tests: it plays for the whole screen, and the split starts when it ends): the game ended the cutscene at once in split screen; it is let through now (`[options] intro_cutscene=0` turns it off). Not looked at: skipping it with a button, a lap knockout or a drag race, the cutscene with 3 or 4 players, a Restart Race (does the cutscene play again?), and 16 cars (the game's opponent characters, the car pool).
- **Drag races in split screen**: the race options show now (the game made the race at once and went to Press Start), the HUDs of every player are drag HUDs, a player whose car was totaled or whose engine blew can open the pause menu, and the controller of a blown engine stops shaking after 3 seconds. Please look at the HUD of players 1 and 2 (now placed in their cells) and at whether the pause menu opens in every case of a player being done (finished, totaled, engine blown, knocked out). A refused pause request is logged.
- **Remembered cars** (needs `more_players=1`): each player's Car Select now starts on the car they accepted last time, also after restarting the game (`[player_N] car=` in the split screen .ini, the game's own handle of the car: it follows the car, not its place in the list, and if the car is not in the list any more the screen starts on the first car). Tested with 3 players over two game starts (log: handles and `Car Select: player N starts on the car they chose before`). Please check it with the real thing, including Career / Bonus / Custom cars, and a player who has never chosen.
- **Cancelling a car selection in the last round** (3 players, or 5, ...): Back on player 3's Car Select now goes back to their Press Start, as it does for the other players, instead of starting the race. Checked with a test run (Back, then choose again and the race starts); try it with a real controller too, and also Back at the player 3 Press Start screen, which I did not look at.
- **Minimaps with black squares** (players 3 and 4 far from players 1 and 2): the map picture's tile manager had room for 9 tiles, which is two or three minimaps' worth, and now has room for four tiles per player. In a test with the cars 2.4 km apart the minimaps wanted 12 tiles at once and all showed their map. Please drive apart and look at all the minimaps.

### 3 or more player issues:
These are just issues stemming from 4-player split-screen not being thoroughly playtested yet. Reverting changes is likely not going to help.
- Players 3 and onwards do not have any audio whatsoever. (Looked into, not attempted: the game's engine sound code only attaches two player cars and assumes exactly two in several places; see agent_docs/split-screen.md "Audio of players 3 and up". Nothing of it can be tested without listening, so I left it for when you are awake.)
- Players 3 and onwards do not get particle effects like sparks whatsoever. (I could not reproduce this: in a test run with the cars apart, sparks from a wall scrape and tire smoke show in the pictures of players 3 and 4. Maybe it was fixed with the camera change. Please check and delete the line if it is fine.)
- Geometry streaming seems inconsistent - Even player 1 and player 2 can end up with geometry that isn't loaded, see screenshot. Notice the zig-zag shape in the top-right, player 2's screen. This happened earlier for player 1 as well. The zig-zag is showing a road whose triangles just don't exist. Driving forward here causes the car to be reset.

### Low prio - Nice-to-haves
- Lap Knockout works weirdly. It doesn't show the option for AI opponents, but it spawns AI opponents anyways. And it seems like players are not getting eliminated.
- HUD scale acts funny with the rearview mirror... (not looked at: I could not see what "funny" means in a test run)
- Would be nice if HUD scale also allowed >100% (not attempted: each HUD is drawn in a viewport of the whole picture at the scale, which cannot be bigger than the screen; it would need a viewport for each corner)
- Exiting a split-screen race should let us switch to a different race type without having to re-select our cars. The UX flow here is just awkward... (Partly helped by the remembered cars: the Car Select starts on your last car, so it is Press Start + Accept for each player. The Press Start screens are still there: I did not try to skip them.)
- How about vertical split screen? ;) (not attempted)
- Heat Level (cops) is the one Quick Race option not available in Split Screen yet (the others are above, under "Needs your testing"). It is not a row of the game: Redux v3's NFSMWRaceFixes adds it in its own code, so it needs that plugin's row class.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

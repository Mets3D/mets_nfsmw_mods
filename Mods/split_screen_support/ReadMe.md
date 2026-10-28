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
- **Car starting positions** (3 or more players): the cars of players 3 and up are now put on the road the way the game puts the NPC opponents of a Quick Race (a row of two behind players 1 and 2 for each pair of players, a single car in the middle of its row, rows 7 m apart, following the road's curve), instead of in a straight line 9 m behind. Checked in the log and screenshots for 3, 4 and 5 players on three tracks; I have not driven it. Please look for cars that start inside a wall or a barrier, or off the road, and for cars that overlap, on tracks with a curve at the start. Restart Race (not tried) should put everybody back in the same places.
- **Streaming** (road triangles missing, car reset when driving into them): I could not reproduce the missing geometry. I fixed what looked wrong: the cars of players 3 and up that the streamer follows had no speed (so no look-ahead of the next zone), and swapping which car a streaming slot follows overwrote the streamer's own state. The streamer still has only two places, so cars far apart (3 groups, more than about a kilometre apart) will still lose track somewhere. If you see it again, tell me where the cars were relative to each other.
- **Quick Race options in Split Screen** (Opponents, Difficulty, Catch Up in the race options, and the AI opponents themselves): they now show in the options of a split screen race and the opponents race with the players. Opponents is the number of AI cars, as in a Quick Race (the row's value is shared with Quick Race: if you have 3 there, you now get 3 AI cars in split screen too until you set it to 0). Checked with 2 players + 3 opponents and 4 players + 3 opponents (logs, a screenshot). Please look at: do the AI cars drive the race properly with the players (laps, finish, race positions), do the extra players start behind the AI cars, anything odd in the results screen. The `[options] ai_in_split_screen=0` setting turns it all off.
- **Remembered cars** (needs `more_players=1`): each player's Car Select now starts on the car they accepted last time, also after restarting the game (`[player_N] car=` in the split screen .ini, the game's own handle of the car: it follows the car, not its place in the list, and if the car is not in the list any more the screen starts on the first car). Tested with 3 players over two game starts (log: handles and `Car Select: player N starts on the car they chose before`). Please check it with the real thing, including Career / Bonus / Custom cars, and a player who has never chosen.
- **Cancelling a car selection in the last round** (3 players, or 5, ...): Back on player 3's Car Select now goes back to their Press Start, as it does for the other players, instead of starting the race. Checked with a test run (Back, then choose again and the race starts); try it with a real controller too, and also Back at the player 3 Press Start screen, which I did not look at.
- **Minimaps with black squares** (players 3 and 4 far from players 1 and 2): the map picture's tile manager had room for 9 tiles, which is two or three minimaps' worth, and now has room for four tiles per player. In a test with the cars 2.4 km apart the minimaps wanted 12 tiles at once and all showed their map. Please drive apart and look at all the minimaps.

### 3 or more player issues:
These are just issues stemming from 4-player split-screen not being thoroughly playtested yet. Reverting changes is likely not going to help.
- Players 3 and onwards do not have any audio whatsoever. (Looked into, not attempted: the game's engine sound code only attaches two player cars and assumes exactly two in several places; see agent_docs/split-screen.md "Audio of players 3 and up". Nothing of it can be tested without listening, so I left it for when you are awake.)
- Players 3 and onwards do not get particle effects like sparks whatsoever. (I could not reproduce this: in a test run with the cars apart, sparks from a wall scrape and tire smoke show in the pictures of players 3 and 4. Maybe it was fixed with the camera change. Please check and delete the line if it is fine.)
- Geometry streaming seems inconsistent - Even player 1 and player 2 can end up with geometry that isn't loaded, see screenshot. Notice the zig-zag shape in the top-right, player 2's screen. This happened earlier for player 1 as well. The zig-zag is showing a road whose triangles just don't exist. Driving forward here causes the car to be reset.

### Low prio - Nice-to-haves
- HUD scale acts funny with the rearview mirror...
- Would be nice if HUD scale also allowed >100%
- Exiting a split-screen race should let us switch to a different race type without having to re-select our cars. The UX flow here is just awkward...
- How about vertical split screen? ;)
- Heat Level (cops) is the one Quick Race option not available in Split Screen yet (the others are above, under "Needs your testing").

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

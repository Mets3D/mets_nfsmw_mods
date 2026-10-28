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
- **Cancelling a car selection in the last round** (3 players, or 5, ...): Back on player 3's Car Select now goes back to their Press Start, as it does for the other players, instead of starting the race. Checked with a test run (Back, then choose again and the race starts); try it with a real controller too, and also Back at the player 3 Press Start screen, which I did not look at.

### 3 or more player issues:
These are just issues stemming from 4-player split-screen not being thoroughly playtested yet. Reverting changes is likely not going to help.
- Players 3 and onwards do not have any audio whatsoever.
- Players 3 and onwards do not get particle effects like sparks whatsoever.
- Geometry streaming seems inconsistent - Even player 1 and player 2 can end up with geometry that isn't loaded, see screenshot. Notice the zig-zag shape in the top-right, player 2's screen. This happened earlier for player 1 as well. The zig-zag is showing a road whose triangles just don't exist. Driving forward here causes the car to be reset.
- The minimaps can have a similar issue - Player 3 and 4 can end up with black squares, missing chunks, which seems to fix when player 1 or 2 is nearby.

### Low prio - Nice-to-haves
- HUD scale acts funny with the rearview mirror...
- Would be nice if HUD scale also allowed >100%
- Exiting a split-screen race should let us switch to a different race type without having to re-select our cars. The UX flow here is just awkward...
- Would be nice if we remembered the selected car for each player across sessions. Note that car selection is 2-dimensional: Up and down chooses category of car, from Career, Bonus, and the 3rd cateogry I forget the name of, Custom or whatever. Then left/right to cycle the cars in a given category. So I'm not exactly sure what state the game needs to remember a car, but probably at least those two. If we just store a car by these indexes, I think that is good enough for now, even if that means if a new car is added, the remembered car may shift. Of course, if cars have a unique ID that we can use to remember them accurately, then let's do that, but I sort of doubt this is the case.
- How about vertical split screen? ;)
- Port the options from Quick Race to Split Screen: NPC opponents, Difficulty, Catch-Up, Heat Level.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

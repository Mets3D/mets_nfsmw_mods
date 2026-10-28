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
- **Streaming** (road triangles missing, car reset when driving into them): a player far from the others had no part of the world loaded around them, because the game's track streamer keeps the track around two positions only. Every player has one now: the cars that have none of the two get an extra place of their own (their sections are added to the list the game makes). In a test with the cars where a game that showed the problem had them (the dev_tools scenario `split_race_4_streaming`) the road of the far player is there and loads in a moment. The streamer's memory is about 1 GB now instead of 242 MB (`[options] streaming_pool_mb`; the game's own size is nearly filled by two places far apart). The game is a 32 bit program with 4 GB of address space, of which a race of four players uses about 3.4 GB: the size is chosen when the game starts so that 2.4 GB of the address space stay free (`streaming_pool_keep_mb`), and the log says what it was (`streaming pool:`, and a line with the address space every 30 seconds of a race). With 1 GB the game crashed once at its start (not enough room for its own allocations); that is why it is chosen that way. F11 writes a dump to the log: the streamer, the memory, and where every car is (a `teleport` line each). Not tried yet: all four cars in different places, a long drive over zone borders, many races in a row. If you see missing geometry again, press F11 and tell me roughly where the cars were.
- **Crash in a long session with the cars spread out** (twice, in the texture loading: once in a minimap tile, once in a section of the track): the game keeps its texture packs in a list of 128 places, and the cars, the minimap tiles and the loaded parts of the track fill it (a tour of the track with 16 racers got to 109, and a game with four players driving apart gets further). It is 1024 now (`[options] texture_pack_slots`). Not repeated since: please drive the four players apart for a long time again. The log has the slots in use every 30 seconds (`streaming: texture pack slots`). The minimaps also leave a tile out when the game's main heap has no room for it (a precaution).
- **Customization of the cars with opponents in the race** (open): with 4 racers (no opponents) player 2's BMW is blue and red; with 5 racers or more it comes in another paint on every run, with 16 racers too. Found so far: it is not memory. Also the opponents: the game picks 3 cars and repeats them (12 opponents are 3 models). Flickering shadows at the start with many cars: not looked into.
- **Quick Race options in Split Screen** (Total Racers, Difficulty, Catch Up in the race options, and the AI opponents themselves): they now show in the options of a split screen race and the opponents race with the players. The Opponents row is replaced by **Total Racers** (the players and the AI cars together, from the number of players up to 16; the number of AI cars is the total minus the players, and the total is raised when you add players). It starts at the players plus 3 and is not shared with the single player Quick Race. Checked with 2 players + 3 opponents and 4 players + 3 opponents (logs, a screenshot); the row itself and 16 racers are not seen yet. Please look at: do the AI cars drive the race properly with the players (laps, finish, race positions), anything odd in the results screen, and with a lot of cars: does the game hold up (car memory, loading). The `[options] ai_in_split_screen=0` setting turns it all off.
- **Cancelling a car selection in the last round** (3 players, or 5, ...): Back on player 3's Car Select now goes back to their Press Start, as it does for the other players, instead of starting the race. Checked with a test run (Back, then choose again and the race starts); try it with a real controller too, and also Back at the player 3 Press Start screen, which I did not look at.
- **Minimaps with black squares** (players 3 and 4 far from players 1 and 2): the map picture's tile manager had room for 9 tiles, which is two or three minimaps' worth, and now has room for four tiles per player. In a test with the cars 2.4 km apart the minimaps wanted 12 tiles at once and all showed their map. Please drive apart and look at all the minimaps.

### 3 or more player issues:
These are just issues stemming from 4-player split-screen not being thoroughly playtested yet. Reverting changes is likely not going to help.
- Players 3 and onwards do not have any audio whatsoever. (Looked into, not attempted: the game's engine sound code only attaches two player cars and assumes exactly two in several places; see agent_docs/split-screen.md "Audio of players 3 and up". Nothing of it can be tested without listening, so I left it for when you are awake.)
- NPCs are not appearing on the minimap. They should!
- Players 3 and onwards do not get particle effects like sparks whatsoever. (I could not reproduce this: in a test run with the cars apart, sparks from a wall scrape and tire smoke show in the pictures of players 3 and 4. Maybe it was fixed with the camera change. Please check and delete the line if it is fine.)
- Geometry streaming seems inconsistent - Even player 1 and player 2 can end up with geometry that isn't loaded, see screenshot. Notice the zig-zag shape in the top-right, player 2's screen. This happened earlier for player 1 as well. The zig-zag is showing a road whose triangles just don't exist. Driving forward here causes the car to be reset.
- Some AI-controlled cars have no engine sounds, even in single-player. Presumably any cars beyond the first 4? But I haven't confirmed that. I'd like a 16-car race's start to sound like a cacophony of different engines, it'd be chaotic and awesome.vvv

### Low prio - Nice-to-haves
- Lap Knockout works weirdly. It doesn't show the option for AI opponents, but it spawns AI opponents anyways. And it seems like players are not getting eliminated.
- Maybe a knocked out player in either lap knockout or drag race should be removed from the screen after a few seconds? Although this would be too jarring if it happened suddenly, and animating a screen re-organization would surely be crazy. But... what if it works? >.>
- We can open the world-map in split-screen/custom race, but we only see player 1. Bit sad.
- HUD scale acts funny with the rearview mirror... (not looked at: I could not see what "funny" means in a test run)
- Would be nice if HUD scale also allowed >100% (not attempted: each HUD is drawn in a viewport of the whole picture at the scale, which cannot be bigger than the screen; it would need a viewport for each corner)
- Exiting a split-screen race should let us switch to a different race type without having to re-select our cars. The UX flow here is just awkward... (Partly helped by the remembered cars: the Car Select starts on your last car, so it is Press Start + Accept for each player. The Press Start screens are still there: I did not try to skip them.)
- How about vertical split screen? ;) (not attempted)
- Heat Level (cops) is the one Quick Race option not available in Split Screen yet (the others are above, under "Needs your testing"). It is not a row of the game: Redux v3's NFSMWRaceFixes adds it in its own code, so it needs that plugin's row class.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

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
- **Cancelling a car selection in the last round** (3 players, or 5, ...): Back on player 3's Car Select now goes back to their Press Start, as it does for the other players, instead of starting the race. Checked with a test run (Back, then choose again and the race starts); try it with a real controller too, and also Back at the player 3 Press Start screen, which I did not look at.
- **Opponents' engine sounds in split screen** (`audio_pool_mb`, `csis_slots`, `sound_voices`, `ai_sampled_engines`, `ai_engine_sounds`, `ai_engine_radius`): with more than 3 opponents (2 or more after my last change) none of them had an engine sound. The cause: the game's sound memory (5.4 MB) only fits the engine sounds of about 5 cars at once; an engine whose sound data does not fit is never started, silently. It is 32 MB now, and in automated races of 2 players + 14 opponents and of 4 players + 12 every opponent's engine was started (I can only see that, not hear it). Two more limits next to it got room too: the sound middleware's objects (256 → 1024; 199 were in use with 16 cars) and the voices (64 → 128; 48 in use). Please listen: 2 players with 4, 8 and 14 opponents, 4 players with 12, a drag race. `ai_sampled_engines` (sampled instead of synthesized engines for 2 or fewer opponents) stays: the synthesized ones were silent for another reason. `ai_engine_radius` (metres, default the game's 135) is how near a player an opponent has to be to have a sound.

### 3 or more player issues:
These are just issues stemming from 4-player split-screen not being thoroughly playtested yet. Reverting changes is likely not going to help.

- Players 3 and onwards have no audio (their cars count as opponents for the sound system and their cameras as listeners, but this was never heard to work: the opponents were silent at the time, see above). Check again now that the opponents have sound.

### Low prio - Nice-to-haves
- Lap Knockout works weirdly. It doesn't show the option for AI opponents, but it spawns AI opponents anyways. And it seems like players are not getting eliminated.
- Maybe a knocked out player in either lap knockout or drag race should be removed from the screen after a few seconds? Although this would be too jarring if it happened suddenly, and animating a screen re-organization would surely be crazy. But... what if it works? >.>
- We can open the world-map in split-screen/custom race, but we only see player 1. Bit sad.
- HUD scale acts funny with the rearview mirror... (not looked at: I could not see what "funny" means in a test run)
- Would be nice if HUD scale also allowed >100% (not attempted: each HUD is drawn in a viewport of the whole picture at the scale, which cannot be bigger than the screen; it would need a viewport for each corner)
- Exiting a split-screen race should let us switch to a different race type without having to re-select our cars. The UX flow here is just awkward... (Partly helped by the remembered cars: the Car Select starts on your last car, so it is Press Start + Accept for each player. The Press Start screens are still there: I did not try to skip them.)
    - Honestly what would be absolutely incredible here... is if we went into split-screen in the menu, and each player could just choose their own car simultaneously... A bit crazy, I know, but I'd like to give it a shot at some point.
- How about vertical split screen? ;)
- Heat Level (cops) is the one Quick Race option not available in Split Screen yet (the others are above, under "Needs your testing"). It is not a row of the game: Redux v3's NFSMWRaceFixes adds it in its own code, so it needs that plugin's row class.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

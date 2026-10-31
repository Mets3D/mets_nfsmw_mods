# Split Screen Support

This mod for Need for Speed: Most Wanted 2005 (1.3) implements full Split Screen support.

## How to use

1. Installing [Controller Support](../controller_support/ReadMe.md) mod highly recommended: without it, you can't have both players playing on a controller, one must use the keyboard.
2. Choose **Quick Race > Split Screen**. Each player presses Start (or Enter) on their own device. It can be a keyboard + controller, or 2 controllers. The first device to press Start/Enter will be on top, the 2nd on bottom.
3. Pick the cars, the track and the options, and race.

## Features
- Each player gets their own minimap and minimap color, not present in the base game. With [Custom Menus](../custom_menus/ReadMe.md) installed, each player can hide their own minimap in **Options > Player > Minimap**.
- With [Custom Menus](../custom_menus/ReadMe.md) installed, each player can scale their HUD (also in a game with one player) in **Options > Player > HUD Scale** (40% to 200%): each part of the HUD (race info at the top right, speedometer at the bottom right, minimap at the bottom left) shrinks or grows around its own corner of the player's part of the screen (and is cut at its edge). The rear view mirror scales with it.
- With Custom Menus installed, each player of a split screen race can set their own volume in **Options > Player > Split-Screen Volume** (0% to 100%, saved per player). Every sound of the race is played for the player nearest to it (their engine, tyres, wind, the cars around them, crashes), and that player's setting scales it; music and menus are not touched. So a player who turns it down also quietens the cars of the others while those are near them.
- Each player can fully control their own camera.
- The Car Select screen says which player it is for and with which device ("Player 2 - Xbox One S Controller", the name the Controls menu shows).
- After a race the race options come back: **Accept** starts the next race with the same cars, and **2 Select Car** (X on Xbox, Square on PlayStation) lets the player who pressed it choose another car first. If the number of players is changed, everyone presses Start and chooses a car again.
- Every player can choose from player 1's cars, including those of their Career and their custom cars (the game gives player 2 only the default cars).
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
- There's a goofy issue that the first 4 cars don't have the brakes on during the intro cutscene, so if they start on a hill, they are rolling during the cutscene, and start the race in whatever position they rolled to, hmm... Issue is most obvious in Lap Knockout race 1.
- There's a HUD element which displays messages like "Player 2 knocked out!" or "Lap time: " which appear for incorrect players: It seems player 4 and player 2 get the same messages to pop up
- There's some issue with the "Lap Time" pop-up UI element displaying at the wrong time for at least player 4. It's supposed to display when the player completes a lap. I think once it happened when another player was completing their lap, but other times it seemed kinda random - perhaps when an AI opponent is completing their lap? Ideally we'd find the function that draws this, and log when it gets drawn, to hunt down the issue. Unless the issue is obvious in the first place.
- I changed my mind about the "Total Racers" option. I think it should be refactored to "AI Opponents" again, but the values should be nudged such that the max total is still only 16. So if Players is set to 2, allow AI Opponents to be set to 14, but any time Players is changed, update AI Opponents such that their sum never exceeds 16.
- AI opponent options are not displayed in most race settings: "Total Racers (to be changed to AI Opponents)", "Difficulty", "Catch Up", are not displayed in Tollbooth and Knockout Options menus. 
- Lap Knockout works weirdly. It doesn't show the option for AI opponents, but it spawns AI opponents anyways. And it seems like players are not getting eliminated. I'm also unsure how the unlimited laps works with this - Do players get eliminated in the first laps, or the final laps?
- Need to test all race types. Never tried tollbooth or speedtrap.
- Maybe a knocked out player in either lap knockout or drag race should be removed from the screen after a few seconds? Although this would be too jarring if it happened suddenly, and animating a screen re-organization would surely be crazy. But... what if it works? >.>
- We can open the world-map in split-screen/custom race, but we only see player 1. Bit sad.
- How about vertical split screen? ;)
- Heat Level (cops) is the one Quick Race option not available in Split Screen yet (the others are above, under "Needs your testing"). It is not a row of the game: Redux v3's NFSMWRaceFixes adds it in its own code, so it needs that plugin's row class.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

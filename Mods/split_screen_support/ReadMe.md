# Split Screen Support

This mod for Need for Speed: Most Wanted 2005 (1.3) implements full Split Screen support.

## How to use

1. Installing [Controller Support](../controller_support/ReadMe.md) mod highly recommended: without it, you can't have both players playing on a controller, one must use the keyboard.
2. Choose **Quick Race > Split Screen**. Each player presses Start (or Enter) on their own device. It can be a keyboard + controller, or 2 controllers. The first device to press Start/Enter will be on top, the 2nd on bottom.
3. Pick the cars, the track and the options, and race.

## Features
Points marked with * require the [Custom Menus](../custom_menus/ReadMe.md) mod to be installed. They're bundled together in the release.
- 2, 3, and 4 player split-screen.
- **Race options**:
  - **Layout**: Horizontal or Vertical split screen.
  - **Start**: Starting position of player cars: Players Front / Players Rear / Random.
  - **AI opponents**: Up to 16 total cars in a race (including players).
  - **Tollbooth: Time**: Make tollbooth races harder or easier by tweaking the available time.
- **Player Options**:
  - Each player can invoke the pause menu. The "Player Options" menu will affect the player who opened the pause menu. The name of the affected player is displayed in the menu's header.
  - **Minimap On/Off**: Each player can toggle their own minimap. All racers appear on the minimap as well as the world map.
  - **HUD Scale**: Each player can scale their own HUD.
  - **Split-Screen Volume**: Each player can tweak how much their screen contributes to the game's audio.
  - **Leaderboard On/Off**: This option already existed, but the game's split-screen HUD didn't include the leaderboard. This mod brings it back, as it's important for Speedtrap races, but can be disabled if you want to free up screen space. Drag races don't show it.
- Tollbooth races are supported: Each player has their own clock, if you run out of time you get knocked out, starting positions work for up to 16 cars (although above 4 it gets unfair), and the end-of-race summary screen shows your remaining time.
- Knockout races work: You cannot specify lap count, it is determined by total racers. When you get knocked out, you no longer control your car, but you can still open the pause menu.
- If a player is eliminated, their screen disappears, one of the neighbouring players takes up the available screen space. The eliminated player's resources are freed, their screen no longer rendered, their audio muted.
- Rear-View Mirrors are allowed to render in split-screen, one for each player. This can be disabled in the game's Advanced Video Options.
- Reduced menu navigation:
  - After a race, the game goes to the race options instead of all the way out into the main menu.
  - After a race, the previously selected cars are remembered, even across sessions.
  - After a race, players don't need to re-select their cars unless the player count setting changes. A new "Select Car" button is added in the Race Options menu. Whichever player presses it, can change their selected car.
  - The Car Select screen and the Player Options menus show which player they're currently changing, eg. "Player 2 - Xbox One S Controller".

## Tested functionality
<details>
<summary>
List of things that could not be taken for granted during development and had to be addressed, but you wouldn't think of them as features.
</summary>

- Races start with the intro cutscenes, just like in single-player.
- World Geometry, shadows, reflections, light trails, sparks, and sun glare, are rendered correctly for each player. The sun was pain in the butt.
- Expanded memory and list sizes for car skin slots, engine sounds, and minimap tiles, to allow 4 players and 16 total racers.
- The engine sounds of all AI opponents plays.
- Sound effects for all players plays, and can even be tweaked in the Player Options menu.
- All players have access to Career cars of the currently loaded save file.
- Each player can control their own camera - which camera preset they're on, looking around with free look from the Controller Support mod, and look-back.
- The mouse looks around only for the player using the keyboard, if any.
- Controller rumble works for all 4 players.
- The camera is offset by a hard-coded amount for players whose screens are wider due to the split-screen layout: Player 1 in 3-player, and both players in 2-player mode.
- Performance optimization culling smaller objects is disabled by default, since it's not needed on modern computers. Can be turned back on in the [.ini](nfsmw_split_screen_support.ini) (`cull_small_objects`).
- All 16 cars spawn in correct places, whether the race has a 2-row (2x2) or a 1-row (1x4) layout, and even in Tollbooth races.
- Rear-view mirrors are working for each player, and they have their own HUD scaling logic to avoid covering up the whole screen.

</details>

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Required for both players using a controller: [Controller Support](../controller_support/ReadMe.md).
- Conditionally compatible: NFSMWExtraOptionsSettings: Set `ExperimentalSplitScreenFix = 0` in its `.ini`.

## Device Compatibility

- Two players using a keyboard is not supported.
- For supported controllers, see the [Controller Support](../controller_support/ReadMe.md) mod - The game natively supports Xbox controllers, and for everything else, Steam Input is tested and works as a translation layer.

---

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

# Split Screen Support

This mod for Need for Speed: Most Wanted 2005 (1.3) implements full split-screen support.

[YouTube link.](https://youtu.be/qJE3Hd06I24)

### Installation
1. Download the latest zip from the [Releases](../../../../releases) page and copy its `scripts` folder into the game's folder (next to `speed.exe`). If you don't want one of the mods, simply delete its `.asi` file.
2. Follow the [instructions for the Controller Support mod](https://github.com/Mets3D/mets_nfsmw_mods/tree/master/Mods/controller_support#installation) to enable Steam Input.
3. Choose **Quick Race > Split Screen**.

Throughout this ReadMe, bullet points marked with * can be disabled in the [.ini file](nfsmw_split_screen_support.ini).

---

## Feature Highlights

- Split-screen is supported for 2, 3, and 4 players.
- *When a player is eliminated from the race, their screen is removed to make space for other players.
- Horizontal and vertical split-screen layouts are available for 2 and 3 players.

https://github.com/user-attachments/assets/96c8153f-56ce-491a-bb4a-8462fbc5392e

## Race Options
- All race types are supported:
    - Circuit, Sprint, and Drag work without any changes. In Drag races, players whose cars are totaled or whose engines are blown are removed from the screen.
    - Speedtrap: The leaderboard HUD makes Speedtrap playable, otherwise you couldn't tell who's in the lead and by how much.
    - Tollbooth: The mod fixes car spawn positions at race start and gives each player their own timer, making Tollbooth playable.
    - Knockout: Lap count is determined by racer count, and knocked-out players are removed from the screen, making Knockout playable.
- **Players**: The number of human player screens.
- **Layout**: Horizontal or vertical split-screen layout.
- **Start**: Starting positions of player cars: Players Front / Players Rear / Random.
- **AI opponents**: Up to 16 total cars in a race (including players).
- **Tollbooth: Time**: Make Tollbooth races harder or easier by tweaking the available time.
<img width="600" src="https://github.com/user-attachments/assets/f540b18b-915a-4cb9-839a-b5bc85455cf5" />

## Player Options
- Each player can invoke the pause menu. The "Player Options" menu will affect the player who opened the pause menu. The name of the affected player is displayed in the menu's header.
- **Minimap On/Off**: Each player can toggle their own minimap. All racers appear on the minimap as well as the world map.
- **HUD Scale**: Each player can scale their own HUD.
- **Split-Screen Volume**: Each player can tweak how much their screen contributes to the game's audio.
- **Leaderboard On/Off**: This option already existed, but the game's split-screen HUD didn't include the leaderboard. This mod brings it back, as it's important for Speedtrap races, but you can disable it if you want to free up screen space. Drag races don't show it.

<img width="600" src="https://github.com/user-attachments/assets/a3f3412f-3f0a-4d32-baf8-d1f2f91ebbf6" />

### Reduced menu navigation
- After a race, the game goes to the race options instead of all the way out to the main menu.
- After a race, the previously selected cars are remembered, even across sessions.
- *After a race, players don't need to re-select their cars unless the player count setting changes. A new "Select Car" button is added in the Race Options menu. Whichever player presses it can change their selected car.
- The Car Select screen and the Player Options menus show which player they currently apply to, e.g. "Player 2 - Xbox One S Controller".

### Tested functionality
<details>
<summary>
List of things that could not be taken for granted during development and had to be addressed, but you wouldn't think of them as features.
</summary>

- *Reflection update rate with 3+ players is clamped at 2/3, regardless of what is set in the Advanced Video Options. This setting just proved to be a massive performance bottleneck for very little gain, particularly with the HD Reflections mod.
- *Races start with the intro cutscenes, just like in single-player.
- World geometry, shadows, reflections, light trails, sparks, and sun glare are rendered correctly for each player. The sun was a pain in the butt.
- Expanded memory and list sizes for car skin slots, engine sounds, and minimap tiles to allow 4 players and 16 total racers.
- The engine sounds of all AI opponents play.
- Sound effects for all players play and can even be tweaked in the Player Options menu.
- Rear-View Mirrors are allowed to render in split-screen, one for each player. This can be disabled in the game's Advanced Video Options. Mirrors have their own HUD scaling logic to avoid covering up the whole screen.
- Each player can control their own camera: choosing a camera preset, looking around with free look from the Controller Support mod, and looking back.
- All players have access to the Career cars in the currently loaded save file.
- The mouse looks around only for the player using the keyboard (if any).
- Controller rumble works for all 4 players.
- The camera is offset by a hard-coded amount for players whose screens are wider due to the split-screen layout: Player 1 in 3-player mode and both players in 2-player mode.
- *The performance optimization that culls smaller objects is disabled by default, since it's not needed on modern computers.
- All 16 cars spawn in the correct places, whether the race has a 2-row (2x2) or a 1-row (1x4) layout, and even in Tollbooth races.
- *The EA Trax song display is hidden so it does not cover player 1's picture. This can be disabled with `hide_ea_trax=0` in the mod's `.ini` file.

</details>

---

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Required for both players to use controllers: [Controller Support](../controller_support/ReadMe.md).
- Conditionally compatible: NFSMWExtraOptionsSettings: Set `ExperimentalSplitScreenFix = 0` in its `.ini`.

## Device Compatibility

- Having two players use a keyboard is not supported.
- For supported controllers, see the [Controller Support](../controller_support/ReadMe.md) mod. The game natively supports Xbox controllers. For everything else, Steam Input has been tested and works as a translation layer.

---

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page were done by hand.

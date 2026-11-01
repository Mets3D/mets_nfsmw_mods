# Mods for Need for Speed: Most Wanted (2005)

These are my mods for NFS: MW 2005 (1.3). My goal with these mods is to improve the game's UX while maintaining the spirit of the original, whatever that means to me. While it should be theoretically possible to cherry pick the mods, it is recommended to use all of them together.

## Finished Mods

| Mod | Description |
|---|---|
| [**Custom Menus**](Mods/custom_menus) | Menu framework: other mods rely on this to add in-game customization options to various menus. Makes menu navigation less cumbersome by removing unnecessary pop-ups and the redundant Career menu. |
| [**Controller Support**](Mods/controller_support) | Analog steering and throttle, camera look, customizable rumble and deadzones, improved button remapping, menu navigation and button prompts, option for PS5 and Switch button prompts. PS5 and Switch controllers tested and fully working via Steam Input. |
| [**Camera Settings**](Mods/camera_settings) | Customize FOV, height, distance, pitch, acceleration response, smoothing, etc, for each camera view in-game, in Options > Camera. A new "Speed FoV" option can add a "vertigo" camera effect to exaggerate the feeling of speed more than before. |
| [**Split Screen Support**](Mods/split_screen_support) | Full local split screen support, improved from even the game's Xbox 360 version, as it **supports 2, 3, or 4 players**. Found in Quick Race > Split Screen. Controller Support mod is needed to support multiple controllers. Other improvements from the original: Each player gets their own minimap, can customize their volume, their HUD scale, and AI opponents can be added (up to 16 total cars supported in a single race, with correct spawn positions and customization). |
| [**Overhaul for Redux v3**](Mods/overhaul_for_redux_v3) | This is the [Overhaul mod by Cyclolocke](https://www.nexusmods.com/needforspeedmostwanted2005/mods/26?tab=description) made compatible with the [Redux v3 mod pack](https://www.youtube.com/watch?v=9nnmeXzm1oQ). |

### Installation
Download the latest zip from the [Releases](../../releases) page and copy its `scripts` folder into the game's folder (next to `speed.exe`). If you don't want one of the mods, simply delete its `.asi` file.

## WIP Mods
These mods are not included in the release .zip, and may never be finished.

| Mod | Description |
|---|---|
| [**Improved AI Opponents**](Mods/improved_ai_opponents/ReadMe.md) | An attempt at improving the AI opponents, not yet successful. |
| [**Dev Tools**](Mods/dev_tools/ReadMe.md) | Dev tools to help work on these mods - insta-load into a save file, crash logging. |


## General Compatibility
- Required: NFS: MW 2005 v1.3. Black Box or not, shouldn't matter, but it must be v1.3.
- Required: [Ultimate ASI Loader (dinput8.dll)](https://github.com/ThirteenAG/Ultimate-ASI-Loader). This ships with most distributions of WideScreenFix, so you probably already have it.
- All my mods were tested with (and without) the [Redux v3 mod pack](https://docs.google.com/spreadsheets/d/e/2PACX-1vTEQaQpo3EscLpyFm7jrr7Q9ivAmBIXN6rklMVuXAsZec0tRRw4MvHnUSD7V2kgyw/pubhtml).
- My mods were also tested and confirmed fully working on Linux with Steam's Proton Experimental.

## AI Disclaimer

The code was written by Claude (Sonnet & Opus). All ReadMe's are human-written, so you're not reading AI slop, unless you read the code.
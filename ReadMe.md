# Met's Mods for Need for Speed: Most Wanted (2005)

This mod pack aims to add a set of features that I always felt were missing from our favourite childhood racing game. I aim to integrate all these changes into the game as smoothly as possible, so that you can't tell vanilla features apart from modded features.

## Mod Pack

| Mod | Description |
|---|---|
| [**Split Screen Support**](Mods/split_screen_support) | Full local split-screen **supporting 2, 3, or 4 players**. Found in Quick Race > Split Screen. All race types are properly implemented. Each player gets their own toggleable minimap, rear-view mirror, volume slider, and HUD scale slider. AI opponents can be added, up to 16 total cars in a race. |
| [**Controller Support**](Mods/controller_support) | Analog steering and throttle, camera look, customizable rumble and deadzones, improved button remapping, menu navigation and button prompts, and an option for PS5 and Switch button prompts. PS5 and Switch controllers have been tested and are fully working via Steam Input. |
| [**Camera Settings**](Mods/camera_settings) | Customize FOV, height, distance, pitch, acceleration response, smoothing, etc., for each camera view in-game, in Options > Camera. A new "Speed FoV" option can add a "vertigo" camera effect to exaggerate the feeling of speed more than before. |
| [**Custom Menus**](Mods/custom_menus) | Menu framework: other mods rely on this to add in-game customization options to various menus. Makes menu navigation less cumbersome by removing unnecessary pop-ups and the redundant Career menu. |

### Installation
Download the latest zip from the [Releases](../../releases) page and copy its `scripts` folder into the game's folder (next to `speed.exe`). If you don't want one of the mods, simply delete its `.asi` file, but it's highly recommended to use all these mods together.

## General Compatibility
- Required: NFS: MW 2005 v1.3. Whether it's Black Box or not shouldn't matter, but it must be v1.3.
- Required: [Ultimate ASI Loader (dinput8.dll)](https://github.com/ThirteenAG/Ultimate-ASI-Loader). This ships with most distributions of WideScreenFix, so you probably already have it.
- All my mods were tested with (and without) the [Redux v3 mod pack](https://docs.google.com/spreadsheets/d/e/2PACX-1vTEQaQpo3EscLpyFm7jrr7Q9ivAmBIXN6rklMVuXAsZec0tRRw4MvHnUSD7V2kgyw/pubhtml).
- My mods were also tested and confirmed to be fully working on Linux with Steam's Proton Experimental.

---

### Bonus: [NFS MW Overhaul for Redux v3](Mods/overhaul_for_redux_v3)
This is the [Overhaul mod by Cyclolocke](https://www.nexusmods.com/needforspeedmostwanted2005/mods/26?tab=description) merged with the [Redux v3 mod pack](https://www.youtube.com/watch?v=9nnmeXzm1oQ).

### WIP Mods
These mods are not included in the release `.zip` file.

| Mod | Description |
|---|---|
| [**Improved AI Opponents**](Mods/improved_ai_opponents/ReadMe.md) | An attempt at improving the AI opponents that has not yet been successful. Also lets you race AI opponents (both vanilla and modded) in split-screen, so you can see what they're doing. |
| [**Dev Tools**](Mods/dev_tools/ReadMe.md) | Dev tools to help with work on these mods: instant loading into a save file and crash logging. |

## AI Disclaimer

The code was written by Claude (Sonnet & Opus). All ReadMes are human-written, so you're not reading AI slop unless you read the code.
# Mods for Need for Speed: Most Wanted (2005)

These are my mods for NFS: MW 2005 (1.3). My goal with these mods is to improve the game's UX while maintaining the spirit of the original, whatever that means to me. All my mods integrate well with each other, and were also tested in the Redux v3 mod pack, although some .ini tweaks may be needed.

| Mod | Description |
|---|---|
| [**Custom Menus**](Mods/custom_menus/ReadMe.md) | Menu framework: other mods rely on this to add in-game customization options to various menus. Makes menu navigation faster by removing unnecessary pop-ups, and letting you skip past the Career menu. |
| [**Controller Support**](Mods/controller_support/ReadMe.md) | Analog steering and throttle, camera look, customizable rumble and deadzones, improved button remapping, menu navigation and button prompts, option for PS5 button prompts. PS5 controller confirmed fully working via Steam Input. |
| [**Split Screen Support**](Mods/split_screen_support/ReadMe.md) | Full local split screen support, found in Quick Race > Split Screen. Use with Controller Support mod to support multiple controllers, each with their own button mapping, rumble, deadzones, even different button prompts (PS5/Xbox). Each player gets their own minimap. |
| [**Camera Settings**](Mods/camera_settings/ReadMe.md) | Customize FOV, height, distance, pitch, acceleration response, smoothing, etc, for each camera view in-game, in Options > Camera. A new "Speed FoV" option can add a "vertigo" camera effect to exaggerate the feeling of speed more than before. |
| [**Dev Tools**](Mods/dev_tools/ReadMe.md) | Dev tools to help work on these mods - insta-load into a save file, crash logging. |

## Installation
Download the latest `mets_nfsmw_mods_<version>.zip` from the [Releases](../../releases) page and copy its `scripts` folder into the game's folder (next to `speed.exe`). It has Custom Menus, Controller Support, Split Screen Support and Camera Settings; Dev Tools is not included in the release.

## General Compatibility
- Required: NFS: MW 2005 v1.3. Black Box or not, shouldn't matter.
- Required: [Ultimate ASI Loader (dinput8.dll)](https://github.com/ThirteenAG/Ultimate-ASI-Loader). (This ships with most distributions of WideScreenFix, so you probably already have it.)
- All my mods were tested with (and without) the [Redux v3 mod pack](https://docs.google.com/spreadsheets/d/e/2PACX-1vTEQaQpo3EscLpyFm7jrr7Q9ivAmBIXN6rklMVuXAsZec0tRRw4MvHnUSD7V2kgyw/pubhtml). Some .ini tweaks needed, mentioned in the ReadMe of each mod.
- My mods were also tested and confirmed fully working on Linux with Steam's Proton Experimental.

## AI Disclaimer

The code was written by Claude (Sonnet & Opus). All ReadMe's are human-written, so you're not reading AI slop.
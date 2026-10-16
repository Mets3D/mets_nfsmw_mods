# Mods for Need for Speed: Most Wanted (2005)

This repo contains some mods for NFS: MW 2005 (1.3) that I vibe coded using Claude Sonnet/Opus.
I try to make these mods feel like a native part of the game. See the page of each mod for more info.

| Mod | Description |
|---|---|
| [**Custom Menus**](Mods/custom_menus/ReadMe.md) | Menu framework: other mods rely on this to add in-game customization options to various menus. |
| [**Controller Support**](Mods/controller_support/ReadMe.md) | Analog steering and separate triggers, camera look, customizable rumble. Xbox controller supported natively, PS5 controller tested fully working via Steam Input. |
| [**Split Screen Support**](Mods/split_screen_support/ReadMe.md) | Full local split screen support, found in Quick Race > Split Screen. Tested with different input devices, rumble and remapping works for each device, and each player gets their own minimap. |
| [**Camera Settings**](Mods/camera_settings/ReadMe.md) | Customize FOV, height, distance, etc, for each camera view in-game, in Options > Camera. |
| [**Dev Tools**](Mods/dev_tools/ReadMe.md) | Dev tools to help work on these mods. |

## Installation
Download the latest `mets_nfsmw_mods_<version>.zip` from the [Releases](../../releases) page and copy its `scripts` folder into the game's folder (next to `speed.exe`). It has Custom Menus, Controller Support, Split Screen Support and Camera Settings; Dev Tools is only for making mods and has to be built from this repo.

## General Requirements
- Required: NFS: MW 2005 v1.3. Black Box or not, shouldn't matter.
- Required: [Ultimate ASI Loader (dinput8.dll)](https://github.com/ThirteenAG/Ultimate-ASI-Loader). (This ships with most distributions of WideScreenFix, so you probably already have it.)
- All my mods were tested with the [Redux v3 mod pack](https://docs.google.com/spreadsheets/d/e/2PACX-1vTEQaQpo3EscLpyFm7jrr7Q9ivAmBIXN6rklMVuXAsZec0tRRw4MvHnUSD7V2kgyw/pubhtml). Some .ini tweaks needed, mentioned in the ReadMe of each mod.
- My mods were also tested and confirmed fully working on Linux with Steam's Proton Experimental.
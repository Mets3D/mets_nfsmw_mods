# Camera Settings

*Disclaimer: The code was written by Claude (Sonnet & Opus). The custom menu icon, testing, design, and this page, were made by me, a human.*

This mod for Need for Speed: Most Wanted 2005 (1.3) lets you tune camera views in the pause menu. You should also grab [Custom Menus](../custom_menus/ReadMe.md) to get these camera settings properly integrated in the game's pause menu. Otherwise, you have to hand-edit `scripts/mets_mods/nfsmw_camera_settings.ini`.

## What it does

[YouTube link.](https://www.youtube.com/watch?v=G5I_quac7Gc)
![The Options > Camera page in the pause menu](docs/camera-options-menu.png)

**Pause > Options > Camera** (and the main menu's Options > Camera) edits the settings of the currently active camera view, which can also be changed as the first option.

In the main menu there is no car, so the values you see are the BMW M3 GTR's. The changes are stored as offsets like always, so they apply to whichever car you drive.

## How it works

In the base game, every car has its own camera values. This mod preserves that by storing only offsets from those base values. While editing the values in the Camera menu however, you're seeing the final calculated value. This means your camera values may change when switching cars. This is normal, and this allows preserving the game's original designed base values, while only applying an offset to those as a way of customization. This is important for eg. hood heights, which differ significantly per car.

## Features

It was my goal to integrate this mod into the original game's design as if it was a native feature that shipped with the game. If used with the Custom Menus mod, the Camera menu has:
- Separate customization for each camera type, including additional cameras added by other mods.
- You can hold Shift or LB to change values with smaller increments.
- Cycling cameras:
  - Renames the vanilla game's "Change Camera View" keybind to **"Next Camera View"** and adds a **Previous Camera View** counterpart. This requires the [Custom Menus](../custom_menus/ReadMe.md) mod.
  - A camera can be set to Enabled=Off in its options. This way, Previous/Next camera keybind will skip that camera.
- Mirror:
  - The vanilla game's "Player -> Rearview Mirror" option is removed.
  - Instead, each camera has a "Mirror" toggle.
- Native feel:
  - A custom hand-made icon for the menu.
  - Hit **Apply** to save. Otherwise, hitting **Back** will ask if you want to discard your changes.
  - A button to reset to defaults.
  - A help button that explains what each setting does.
  - Controller input tested and fully supported.
- **Speed FoV**: This option adds additional field of view based on your speed. It's great for making the game feel more intense. The base game doesn't do this. It's disabled by default.

## Settings, files

After launching the game with the mod installed, it will store camera settings in `installdir/scripts/mets_mods/nfsmw_camera_settings.ini`. This file is also what gets modified when using the in-game Camera menu. `nfsmw_camera_settings.log` writes debug data here.

## Mod Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- Recommended: [Custom Menus](../custom_menus/ReadMe.md) mod if you want to customize cameras in-game instead of digging in an .ini file.
- Incompatible: Other mods that change `CameraAnchor::GetPov`
- Confirmed compatible:
  - "DRIFT" and "Pursuit" camera modes added by [`NFSMWExtraOptions`](https://github.com/ExOptsTeam/NFSMWExOpts).

## Ideas / TODO
- Ship the mod with a preset .ini with my recommended defaults.
- I'm not convinced that the "Tilting" option works.
  - Pretty much confirmed, it either doesn't work or doesn't do what I think.
  - This raises my suspicion that the Stiffness also does not work.
- I don't get why "Distance / Lag" is one option... These feel like two incredibly distinct and unrelated concepts to me.
- A way to try values while driving without pausing.
- Update the YT link above, possibly make it an embed.
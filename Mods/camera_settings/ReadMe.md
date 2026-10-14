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
- A custom hand-made icon.
- Separate customization for each camera type, applied as offsets to the current car's base camera values.
- Hit **Apply** to save. Otherwise, hitting **Back** will ask if you want to discard your changes.
- A "Speed FoV" slider (0 = off, up to 50 degrees): extra FoV added on top of the base as you speed up, reaching the full amount at 300 km/h. Set per camera view.
- A button to reset to defaults.
- **Previous Camera View**: the game's "Change Camera View" control is renamed "Next Camera View", and a new "Previous Camera View" row sits under it in **Options > Controls** (needs [Custom Menus](../custom_menus/ReadMe.md); bound to nothing by default). It scrolls through the camera views backwards, for the keyboard or a controller. On a controller it is D-pad Left if [Controller Support](../controller_support/ReadMe.md) is installed (its default layout frees the D-pad); otherwise the binding is empty. The binding is saved in `nfsmw_camera_settings.ini` (`[previous_camera]`), not in the game's profile.
- A help button that explains what the current setting does.
- Controller input supported for all of the above.
- You can hold Shift or LB to change with smaller increments.

## Settings, files

After launching the game with the mod installed, it will store camera settings in `installdir/scripts/mets_mods/nfsmw_camera_settings.ini`. This file is also what gets modified when using the in-game Camera menu. `nfsmw_camera_settings.log` writes debug data here.

## Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- Recommended: [Custom Menus](../custom_menus/ReadMe.md) mod if you want to customize cameras in-game instead of digging in an .ini file.
- Incompatible: Other mods that change `CameraAnchor::GetPov`
- Confirmed compatible:
  - "DRIFT" and "Pursuit" camera modes added by [`NFSMWExtraOptions`](https://github.com/ExOptsTeam/NFSMWExOpts).

## Ideas / TODO
- A way to try values while driving without pausing.
- Ability to disable certain camera views from the previous/next rotation?
- Update the YT link above, possibly make it an embed.
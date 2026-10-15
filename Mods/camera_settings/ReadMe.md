# Camera Settings

*Disclaimer: The code was written by Claude (Sonnet & Opus). The custom menu icon, testing, design, and this page, were made by me, a human.*

This mod for Need for Speed: Most Wanted 2005 (1.3) lets you tune camera views in the pause menu. You should also grab [Custom Menus](../custom_menus/ReadMe.md) to get these camera settings properly integrated in the game's pause menu. Otherwise, you have to hand-edit `scripts/mets_mods/nfsmw_camera_settings.ini`.

## What it does

[YouTube link.](https://www.youtube.com/watch?v=G5I_quac7Gc)
![The Options > Camera page in the pause menu](docs/camera-options-menu.png)

**Pause > Options > Camera** (and the main menu's Options > Camera) edits the settings of the currently active camera view, which can also be changed as the first option.

In the main menu there is no car, so the values you see are the BMW M3 GTR's. The changes are stored as offsets like always, so they apply to whichever car you drive.


## Features

It was my goal to integrate this mod into the original game's design as if it was a native feature that shipped with the game. If used with the Custom Menus mod, the Camera menu has:
- Separate customization for each camera type, including additional cameras added by other mods. Cameras have these options:
  - FoV, Height, Forward/Backward Distance, Left-Right lateral offset.
  - Smoothing
  - Tilt: How much the camera tilts with the car.
  - Acceleration: How much the camera's forward/backward distance reacts to acceleration and braking.
  - Speed FoV: Additional FoV / "vertigo effect" based on your speed.
  - Mirror: Whether a rear-view mirror is visible in this view. (The vanilla game's "Player -> Rearview Mirror" option is removed.)
  - Enabled: Whether this camera view is used by the Next/Previous Camera View hotkeys.
- You can hold Shift or LB to change values with smaller increments.
- Cycling cameras: Renames the vanilla game's "Change Camera View" keybind to **"Next Camera View"** and adds a **Previous Camera View** counterpart. This requires the [Custom Menus](../custom_menus/ReadMe.md) mod.
- Values are applied as offsets, but displayed as absolute:
  - In the game, every car has base camera positions for each camera view. This is preserved by applying your customizations as an offset.
  - This does mean that the displayed camera values can change when you change cars. This is working as intended. The same offsets were applied to a different base, and the final value is displayed.
  - This also means we must pick a base car to use when displaying the camera settings in the main menu. For this, we arbitrarily picked the BMW M3 GTR.
- Native UX:
  - A custom hand-made icon for the menu.
  - Hit **Apply** to save. Otherwise, hitting **Back** will ask if you want to discard your changes.
  - A button to reset to defaults.
  - A help button that explains what each setting does.
  - Controller input tested and fully supported.

## Mod Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- Recommended: [Custom Menus](../custom_menus/ReadMe.md) mod if you want to customize cameras in-game instead of digging in an .ini file.
- Incompatible: Other mods that change `CameraAnchor::GetPov`
- Confirmed compatible:
  - "DRIFT" and "Pursuit" camera modes added by [`NFSMWExtraOptions`](https://github.com/ExOptsTeam/NFSMWExOpts).

## Ideas / TODO
- Custom Menus mod should probably be a requirement, and maybe we should raise an error if it's not installed.
- Ship the mod with a preset .ini with my recommended defaults.
- A way to try values while driving without pausing.
- Update the YT link above, possibly make it an embed.
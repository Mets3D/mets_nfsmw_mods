# Camera Settings

This mod for Need for Speed: Most Wanted 2005 (1.3) lets you tune camera views in the pause menu and main menu.

[YouTube link.](https://www.youtube.com/watch?v=F0Cw4eQ8czg)

## Installation
Download the latest zip from the [Releases](../../../../releases) page and copy its `scripts` folder into the game's folder (next to `speed.exe`). If you don't want one of the mods, simply delete its `.asi` file.

## Features

It was my goal to integrate this mod into the original game's design as if it were a native feature that shipped with the game:

https://github.com/user-attachments/assets/dd074c53-44da-489d-bfa8-1defa959118c

- Native UX:
  - A custom handmade icon for the menu.
  - Hit **Apply** to save. Otherwise, hitting **Back** will ask if you want to discard your changes.
  - A button to reset to defaults.
  - A help button that explains what each setting does.
  - Controller input is tested and fully supported.
- Separate customization for each camera type, including additional cameras added by other mods. Each camera view has the following options:
  - FoV, Height, Distance.
  - Speed FoV: Implements a vertigo effect (FoV increase + forward dolly) based on your current speed. Not present in the original game.
  - Smoothing: How loosely or tightly the camera is attached to the car.
  - Tilt: How much the camera tilts with the car. Can have a pretty dramatic effect if your car rolls over.
  - Acceleration: How much the camera's forward/backward distance reacts to the car's acceleration and braking.
  - Mirror: Whether a rear-view mirror is visible in this camera view. The vanilla game's "Player -> Rearview Mirror" option is removed.
  - Enabled: Whether this camera view is used by the Next/Previous Camera View hotkeys.
- You can hold Shift or LB to change values in smaller increments.
<img width="600" alt="image" src="https://github.com/user-attachments/assets/d87f6d5a-8e7b-4207-9d9c-18b80904a2bd" />

- Cycling cameras: The mod renames the vanilla game's "Change Camera View" keybind to **"Next Camera View"** and adds a **Previous Camera View** counterpart.
- Values are applied as offsets, so the game's base camera positions for each car type are preserved. The displayed values are the final values, with the current car's values included. In the main menu, the mod uses the BMW M3 GTR's camera values.

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Required: [Custom Menus](../custom_menus/ReadMe.md). Without it, the customization options won't appear.
- Confirmed compatible:
  - "DRIFT" and "Pursuit" camera modes added by [`NFSMWExtraOptions`](https://github.com/ExOptsTeam/NFSMWExOpts).
- This mod will force `ForceEnableMirror = 0` in the [NFSMWHDReflections](https://github.com/sniten/NFSMWHDReflections) mod, since the mirror can now be toggled per camera instead.
- Using my [Split Screen Support](../split_screen_support/ReadMe.md) mod:
  - Each player can control the position and active camera type of their own camera.
  - The camera setting values, however, are NOT specific to each player.
  - Hard-coded offsets are applied to those players whose aspect ratio is wider or taller than normal because of the split-screen layout.
  - The split-screen mod implements rear-view mirror support for up to 4 players' screens. If the Camera Settings mod is present, it will listen to its per-camera Mirror setting.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). The custom menu icon, testing, design, and this page were done by hand.

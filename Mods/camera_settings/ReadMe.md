# Camera Settings

This mod for Need for Speed: Most Wanted 2005 (1.3) lets you tune camera views in the pause/main menu. See mod requirements below.

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Required: [Custom Menus](../custom_menus/ReadMe.md). Otherwise all the customization options won't appear.
- Incompatible: Other mods that change `CameraAnchor::GetPov`
- Confirmed compatible:
  - "DRIFT" and "Pursuit" camera modes added by [`NFSMWExtraOptions`](https://github.com/ExOptsTeam/NFSMWExOpts).

## What it does

[YouTube link.](https://www.youtube.com/watch?v=G5I_quac7Gc)
![The Options > Camera page in the pause menu](docs/camera-options-menu.png)

**Pause > Options > Camera** (and the main menu's Options > Camera) edits the settings of the currently active camera view, which can also be changed as the first option.

## Features

It was my goal to integrate this mod into the original game's design as if it was a native feature that shipped with the game. Here's what you can expect:

- Separate customization for each camera type, including additional cameras added by other mods. Each camera view has the following options:
  - FoV, Height, Forward/Backward Distance.
  - Smoothing: Reduce this to make the camera stick to the car more, but it will also be a bit more shaky.
  - Tilt: How much the camera tilts with the car.
  - Acceleration: How much the camera's forward/backward distance reacts to acceleration and braking.
  - Speed FoV: Additional FoV / "vertigo effect" based on your speed. This behaviour is not present in the vanilla game.
  - Mirror: Whether a rear-view mirror is visible in this view. (The vanilla game's "Player -> Rearview Mirror" option is removed.)
  - Enabled: Whether this camera view is used by the Next/Previous Camera View hotkeys.
- You can hold Shift or LB to change values with smaller increments.
- Cycling cameras: The mod renames the vanilla game's "Change Camera View" keybind to **"Next Camera View"** and adds a **Previous Camera View** counterpart.
- Values are applied as offsets:
  - In the game, every car has base camera positions for each camera view. This is preserved by applying your customizations as an offset.
  - This does mean that the displayed camera values can change when you change cars. This is working as intended. The same offsets were applied to a different base, and the final value is displayed.
  - This also means we must pick a base car to use when displaying the camera settings in the main menu. For this, we arbitrarily picked the BMW M3 GTR.
- Native UX:
  - A custom hand-made icon for the menu.
  - Hit **Apply** to save. Otherwise, hitting **Back** will ask if you want to discard your changes.
  - A button to reset to defaults.
  - A help button that explains what each setting does.
  - Controller input tested and fully supported.

# TODO
- We should ship our own camera defaults in the ini - Vanilla cameras mostly unchanged/replicated, but the super far, drift, and pursuit cameras could be tweaked, eg. to be disabled tbh.
- I think camera look stopped working, possibly due to DevTools.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). The custom menu icon, testing, design, and this page, were hand-made.

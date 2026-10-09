# Camera Settings

*Disclaimer: Vibe coded with Claude. Thoroughly tested.*

The plugin `nfsmw_camera_settings.asi` for Need for Speed: Most Wanted (2005, PC, `speed.exe` 1.3) that lets you tune every camera view in the pause
menu. It needs [nfsmw_custom_menus](../custom_menus/ReadMe.md) for its menu page. Otherwise, you have to hand-edit `scripts/nfsmw/nfsmw_camera_settings.ini`.

## What it does

![The Options > Camera page in the pause menu](docs/camera-options-menu.png)

**Pause > Options > Camera** edits the camera view the game's own Camera selector points at; row 1 is
that selector, so you pick the view and edit it in the same screen. The values are saved in `nfsmw_camera_settings.ini`.

## How it works

`CameraAnchor::GetPov` builds the active camera view (a POV struct) from the car's attributes every frame. The plugin
changes the fields you edited in what it returns, **relative to the game's own value**: the page shows the value for the
car you drive, and what is saved is the difference. Cars differ a little in the game's data (hood heights, for example),
and some values can move while driving, so every car keeps its own character and a field you set back to the game's value
is no longer a setting. Only Tilting is saved as plain on / off.

## Settings, files

In the folder `scripts/nfsmw/` (next to the `.asi` files) (created on first start; all our mods keep their files there): `nfsmw_camera_settings.ini` (your settings, written by the menu), `nfsmw_camera_settings.log`, `nfsmw_camera_settings.tpk` (the page's
icon, built from `camera_settings/OPTIONS_CAMERA.png` by `set_mods.ps1`).

```ini
[close]            ; one section per view: bumper hood close far super_far drift pursuit
fov=5              ; keys: fov height lag angle lat stiffness tilting. Each is added to the game's value
height=-0.25       ; for the car (Tilting: 1 = on, 0 = off, instead)
[general]
degree_symbol=0    ; write " deg" instead of the degree sign
icon=OPTIONS_CAMERA ; a texture name, or its hash as 8 hex digits (default OPTIONS_CAMERA)
[hooks]
drivecamdraw=0     ; skip one hook (getpov drivecamdraw)
```

## Compatibility

Needs only the ASI loader and `speed.exe` 1.3 (and nfsmw_custom_menus for the page). It shows the extra camera views from
NFSMWExtraOptions when that mod is installed, but does not need it. A mod that changes `CameraAnchor::GetPov` the same way
conflicts (the hook is skipped). See the [overview](../ReadMe.md#compatibility).

## Ideas / TODO
- Take native per-car values into account.
- New "Speed FoV" option: FoV rising with speed, lag falling.
- A way to try values while driving without pausing.
- Add keybind for "Previous Camera"
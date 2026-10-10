# Mods for Need for Speed: Most Wanted (2005)

*Disclaimer: Vibe coded with Claude. Thoroughly tested.*

Plugins for the PC game (`speed.exe` 1.3, 5,926,912 bytes), loaded by Ultimate ASI Loader from the game's `scripts/`
folder. Written in C++17 with MinHook; they hook the game's own functions so everything looks and behaves like part of
the game.

| Mod | What | Docs |
|---|---|---|
| **Custom Menus** (`nfsmw_custom_menus.asi`) | the Options menu framework: other mods add pages to Pause > Options through it; also fixes the Controls screen's title icon | [custom_menus/ReadMe.md](custom_menus/ReadMe.md) |
| **Camera Settings** (`nfsmw_camera_settings.asi`) | camera settings: per-view FOV, height, distance, pitch, offset, stiffness, tilting (Options > Camera) | [camera_settings/ReadMe.md](camera_settings/ReadMe.md) |
| **Controller Support** (`nfsmw_controller_support.asi`) | Xbox controller: analog steering and separate triggers, camera look, rumble (Options > Rumble) | [controller_support/ReadMe.md](controller_support/ReadMe.md) |

`nfsmw_camera_settings` and `nfsmw_controller_support` work without `nfsmw_custom_menus`, only without their menu pages.

## Compatibility

**What they need.** Ultimate ASI Loader and `speed.exe` 1.3 (the hooks check the first bytes of every function they
hook, and skip any that another mod already changed). Nothing else: no mod of ours depends on another mod of someone
else's, and `nfsmw_camera_settings` and `nfsmw_controller_support` do not depend on each other. `nfsmw_custom_menus` is only needed for the menu pages.
They have only been tested with this set of mods in the test copy (and nothing else): BloomResIncreaser,
NextGenGraphics, the widescreen fix (ThirteenAG, 2026-09-12 release), NFSMWExtraOptions, NFSMWFEShadows, NFSMWGraphics,
NFSMWHDContentSupport, NFSMWHDReflections, NFSMWMotionBlurController, NFSMWRaceFixes, NFSMWUnlimiter,
NFSMW_NOAIRSTABILIZER, NFSMW_XenonEffects, SpeedReflect and XNFS-ShaderLoader. Not yet tested: a vanilla install.

**Optional interplay** (nothing breaks without them):
- *The widescreen fix*: with `ImproveGamepadSupport = 1` it gives the colourful button icons, which also show on our Help
  hint (the 2 key glyph appears as X on a controller). Its direction-less binding names are replaced by ours.
- *NFSMWExtraOptions*: its extra camera views (Drift) appear in the camera page; nfsmw_camera_settings shows the Drift view's
  name with the game's own string instead of its all-caps one.

**Known conflicts**
- *Widescreen fix camera look* (`[CAMERA] Enable = 1`): both it and nfsmw_controller_support's camera look redirect the same call in
  `CubicCameraMover::Update`. nfsmw_controller_support notices, logs it and turns its camera look off. Set `Enable = 0` in
  `NFSMostWanted.WidescreenFix.ini` to use ours (which also works in every chase view and with the right stick bound in
  the Controls menu).
- *NFS-XtendedInput*: overlaps with nfsmw_controller_support (analog input, button icons) and rewrites parts of the Options / Controls
  menus (it jumps over code inside `UIOptionsMain::Setup`; in our test the controller options vanished from the menu and an "Online Options" entry appeared). Do not
  use both; it also has no rumble.
- *Any mod that hooks the same game functions* (the Options screens, `CameraAnchor::GetPov`, `LocalPlayer::DoFFB`, the
  controller input path): our hook for that function is skipped and the log says which. For nfsmw_custom_menus one skipped hook
  means no pages at all, because its hooks depend on each other.
- *Another mod that adds Options menu entries*: the menu's icon strip has a fixed number of slots; if they run out, the
  game drops the entry that does not fit (nfsmw_custom_menus logs "no free slot").
- *Mods that change the game's controller device ID or the binding table* are untested; nfsmw_controller_support uses binding entries
  25-28 (the game's unused `SC_A/D/W/S`, renamed `CAM_ACTION_LOOK_*`) and checks their names before touching them.

## Layout

```
Mods/
  common/              header-only helpers all mods share (log, language table, hooks, XInput, the custom menus API)
  custom_menus/        nfsmw_custom_menus: the Options menu framework
  camera_settings/     nfsmw_camera_settings: camera presets (OPTIONS_CAMERA.png -> nfsmw_camera_settings.tpk, the page's icon)
  controller_support/  nfsmw_controller_support: XInput input, camera look, rumble (several files, see its ReadMe)
  set_mods.ps1         deploys to the test copy of the game
```

Where code goes: a feature lives in the mod it is about; anything two mods need goes in `common/`. Two plugins cannot hook
the same game function, so code that touches a shared part of the game (like the Options menu) belongs in a framework
mod the others register with, not in whichever mod happens to need it first.

## Build and test

Needs the MSYS2 MINGW32 toolchain (gcc, cmake, make) and the SDK clone in `../Tools/nfsmw-2005-sdk`.

```
cd Mods
cmake -S . -B build -G "MinGW Makefiles"     # once
cmake --build build                          # build/nfsmw_*.asi
powershell -File set_mods.ps1 -Mode ours      # copy to GameTest/scripts (+ texture packs); -Mode all = only the other mods, none of ours
```

`GameTest/` is a copy of the game; the original install is not touched. Close the game before deploying (it keeps the old
`.asi` loaded). Settings, logs and texture packs of all our mods are in one folder, `scripts/mets_mods/`, so the game folder stays tidy. The logs: `nfsmw_custom_menus.log`, `nfsmw_camera_settings.log`, `nfsmw_controller_support.log`, and
`nfsmw_custom_menus_crash.log` for access violations anywhere in the game.

To uninstall a mod, delete its `.asi` (and `.tpk`) from `scripts/`.

## Adding a mod

1. `Mods/<name>/main.cpp` with `NFSMW_PLUGIN_DECLARE` / `NFSMW_PLUGIN_MAIN` (see `camera_settings/main.cpp` for a small one) and a
   line in `CMakeLists.txt`.
2. Use `common/hooks.h` for hooks (a table of the function's address and first 7 bytes; hooks over a changed function
   are skipped), `common/nfsmw_util.h` for the log, strings and language table.
3. A menu page: export `nfsmw_custom_menus_provide` (see [custom_menus/ReadMe.md](custom_menus/ReadMe.md)).
4. Add the mod to `set_mods.ps1`, and note installs and findings in `../notes.md`.

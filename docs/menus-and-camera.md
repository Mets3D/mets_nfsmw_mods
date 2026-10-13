# Options menu framework, FEng, textures, camera presets (custom_menus, camera_settings)

Marks: [read] [tested]. Addresses speed.exe 1.3.

## Our Options pages (custom_menus)
- Hooks: `UIOptionsMain::Setup`, `IconScroller::SetInitialPos` 0x592C90 (add the Options entry right before it; `UIOptionsMain::Setup` calls it through the row's vtable, inlined),
  `UIOptionsScreen::SetupPlayer` / `NotificationMessage` / `RestoreDefaults` / `OptionsDidNotChange` 0x510360 / `RestoreOriginals` 0x510450, `feDialogScreen::BuildFromConfig` 0x58F720
  (message box title/icon objects 0x0D4F4069 / 0xFAC88427). The screen reuses `SetupPlayer` + `ClearWidgets` and our own `FEToggleWidget` rows (hand-built copy of `PODriveCam`'s 15-slot
  vtable with Act / Draw replaced; widget 0x64 bytes, param index at +0x5C, page pointer at +0x60).
- **Pages are pulled**: the first time the pause menu's Options opens, `custom_menus` calls the export `nfsmw_custom_menus_provide(host)` of every loaded module (Toolhelp snapshot), so load
  order does not matter. A page = rows + callbacks (`common/custom_menus_api.h`); `Page::placement` can instead append rows to the game's own Player options. A mod works without the framework,
  only without its page.
- Apply semantics: the game's options screens edit live and keep `Original*Settings`; Back asks "discard?" via `OptionsDidNotChange`, a confirmed discard calls `RestoreOriginals`, Accept just
  leaves; both end in `FEHASH_EXITCOMPLETE` 0xE1FDE1D1. Pages mirror that: edits in memory, a snapshot from opening, the ini written on EXITCOMPLETE only if something differs.
- Pause menu: `PauseMenu::SetupOptions` 0x52B4D0 builds the row with `IconScrollerMenu::AddOption` 0x573960 (`new pm_X(texture hash, name hash, description hash)`, `IconOption` ctor 0x586FA0).
  The tuning screen is `CustomTuningScreen` (7 `TuningSlider` rows; min -1, max 1, step 0.2).

## FEng and layouts
- Language strings: a sorted `{hash, char*}` table at `*0x91CF80` (count `*0x91CF78`); merging our entries there makes every text path work. Strings are resolved through
  `(*0x91DD08)->vtbl[0]`. Titles are upper case in `LANGUAGES/English.bin`.
- Layouts (FNG) are JDLZ blocks in `GLOBAL/InGameB.bun` (22 `Pause_Main`, 23 `Pause_Options`, 24 `Pause_PCDisplayOptions`, 25 `Pause_Controller`, 26 `Pause_PC_Controller`, 27
  `Pause_Performance_Tuning`); `FrontB.lzc` holds the front-end FNGs uncompressed. Object names are hashes (`HEADERTEXT` 0x42ADB44C, `__HELP_BUTTON__`, `AcceptText`).
- **Cloning FEng objects at runtime**: `FEGroup::Clone` 0x5C49C0 leaves `FEObject::Cached` (+0x58) uninitialised (invisible clone, crash in `FERenderObject::Clear` 0x591A00): zero it on every clone.
  Copy the object data block (+0x2C colour / pos / size / UVs) from the original. `FEngSetCenter` resets z and hides the object: set Pos.x directly. Give each clone a unique NameHash (+0x10) and
  GUID (+0xC): mouse hit areas are offset by searching parents by name hash (`OffsetCalculatron` 0x5B1B60) and responses target objects by GUID. Rebuild the package mouse list
  (`FEPackage::BuildMouseObjectStateList` 0x5B88F0) after adding.
- Hint bars: the Options layout has console groups (hidden on PC) and PC groups {label, clickable hit-box group with FF_MouseObject (1<<17), key image}: Defaults 0x3FC72F9D, Accept 0x9502D148,
  Back 0x523250E9. The Defaults hint uses texture `CIRCLE` (0x54A68051), drawn as the "1" key; `PC_KEY_1..4/ENTER/ESC/M/Q/T` live in `GLOBAL/GLOBALB.BUN`. Hover is script-driven: the hit-box's
  mouse-over script restarts every frame from the 0xB30D0683 response and posts 0x696851AD (HIGHLIGHT), exit runs script 0xCCFA posting 0x1C8E4190 (UNHIGHLIGHT); both target 0xFFFFFFFC (the whole
  package, routed by the `Mt` chunks), so a cloned hint must point those events at its own label by GUID (FEScript events: count +0x24, array +0x28 of {EventID, Target, tTime}; scripts list +0x44).
- The Help key opens `ShowOneButton` 0x5989B0.

## Textures and packs
- A standalone `.tpk` loads through `LoadResourceFile(name, type, flags, callback, param, offset, size)` 0x661760 (cdecl; `LoadingFinishedFlag` at +0x30), advanced by `ServiceResourceLoading`
  0x6626B0. `GetTextureInfo(hash, return_default, include_unloaded)` 0x503400 says whether a texture is registered. The pack's FilenameHash is bHash of its filename. 32-bit texture format entry
  `{0, 0, 1, 5, 6, 21 (A8R8G8B8), 0, 0}`. Options icons are 256x256 with one mip. TPK layout: `0xB3300000` { `0xB3310000` { `0x33310001` header (name at +4), `0x33310004` TextureInfo[0x7C]
  (name +0xC, hash +0x24, placement +0x30) ...}}; see `Tools/tpk_*.py`.
- Menu textures: pause-menu Options icons (`OPTIONS_ICON_*`, 256x256 DXT3/5) are in pack `INGAMETEXTURES` in `GLOBAL/InGameA.bun`; the main menu copies are in `FRONTENDTEXTURES` in
  `FRONTEND/FrontB.lzc` (not loaded in game). `GLOBALTEXTURES(PC)` in `GLOBAL/GlobalB.lzc` are always loaded (PC key glyphs, `MILESTONE_SPEEDTRAP` 0x66C9A7B6). Debug names are cut at 23
  characters, so the bHash of the visible name can differ from the real hash (`INGAME_ICON_PERFORMANCE` = 0x483238FD).

## Boot flow (dev_tools quick start)
- **Startup screen sequence** [read]: `BootFlowManager` (`mInstance` 0x91CF48) steps through a fixed list of FEng packages: `MC_Bootup_GC.fng`, `LS_EAlogo.fng`, `LS_PSA.fng`,
  `MW_LS_AttractFMV.fng`, `MW_LS_Splash.fng`, `MC_Main_GC.fng`, `MainMenu.fng` (decomp `FEBootFlowManager.cpp`). `ChangeToNextBootFlowScreen(mask)` 0x57DBB0 advances `CurrentScreen` to
  whatever follows it; `JumpToScreen(name)` 0x57DAF0 jumps straight to a named one instead. Either reaching the list's tail (`MainMenu.fng`) calls `Destroy` 0x56AD30.
- **Skipping a movie** [read]: the EA logo, the PSA and the attract movie (`LS_EAlogo`/`LS_PSA`/`MW_LS_AttractFMV`, decomp `FEMovieScreen.cpp`) are each an FEng movie-starter widget
  (FEObject type 7). `FEngMovieStarter::Callback` 0x559680 checks the byte at 0x926144 before opening the movie and, if set, posts `0xc3960eb9` ("movie finished") immediately instead -
  the same message a movie that actually played posts when it ends, read by `MovieScreen::NotificationMessage` 0x57DDA0 to call `ChangeToNextBootFlowScreen(0xff)`. Setting that byte once
  at startup skips all three (and any later FEng movie the same way); `NFSMWExtraOptions`'s `SkipMovies` and the widescreen fix's `SkipIntro` set the same byte. Constructing the
  `MovieScreen` itself (ctor 0x58ECA0) does not start the movie - only `Callback` does, later - so skipping from the constructor is too early: it tears down a movie that has already
  started opening rather than one that is finished, which crashes.

## Camera presets (camera_settings)
- `CameraAnchor::GetPov` 0x4713C0 (thiscall, 1 arg = preset 0..6, returns POV*) runs every unpaused frame (~110/s) from `CubicCameraMover::Update` and rebuilds the POV (at anchor+0xC8 on PC) from the car's
  `camerainfo`; hooking it and overwriting the result is the override point. POV layout: type@0 s16, angle@2 u16, lag@4, height@8, lat@0xC, fov@0x10 u16, stiffness@0x14, tilting@0x18.
  `GameFlowManager::IsPaused` 0x64B680 (ecx 0x925e70).
- `GLOBAL/attributes.bin` (VPAK) holds at least 81 `camerainfo` collections (class definition at 0x2b1a0: FOV 0x263E9452, HEIGHT 0x762B7718, STIFFNESS 0x7F8EEA1A, LAG 0xDE0857E3; collections from
  0x2b230, 6 float pairs each); they differ per car and view, and the blend between a pair is not worked out. So the page stores **offsets** from the driven car's value, added in the GetPov hook.

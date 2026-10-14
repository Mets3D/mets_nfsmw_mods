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
- Main menu: the same `UIOptionsMain` (`mCalledFromPauseMenu` +0x16C = 0); `Setup` adds Audio, Video, Gameplay, Player, Controller, then (not online) EA Trax and Credits, each through
  `IconScrollerMenu::AddOption` 0x573960 (a real call; `ecx` = menu, one stack argument; the option's icon hash is at +0xC: EA Trax 0xC52CCBF6, Credits 0x51009E20). Pages with `kNewEntryEverywhere`
  are inserted at the first of those two calls, else at `SetInitialPos` [read; not run in the game].
- **Icon strip slots** [tested]: `UIOptionsMain`'s icons are the layout's images `OPTION_1`..`OPTION_n` (12 in both layouts; the first two and last two entries are the scroller's end markers); `IconScroller::AddOption` 0x573960's callee
  deletes an entry that finds no image. More slots are made by cloning the last image through its vtable slot 1 (`Clone(reference = 0)`), `copy_object_state`, a new name hash (`FEHashUpper("OPTION_i")`) and GUID, and adding it to the original's parent.
  The layout's response to Accept names the original slot's object, so a press on a clone reaches `UIOptionsMain::NotificationMessage` 0x545310 for that object (not the current entry's): it is replaced by the current entry's object there.
  Rows and pages have no limit either (a page with more than 15 rows scrolls its 15 slots; the scroll bar is set from the page's row count after every tick, since the game re-sets it from its widgets).
- Icons: `IconOption`'s constructor gives textures 0x66C9A7B6 (speed trap) and 0xAAAB31E9 (drag) a tutorial movie (flag +0x46, name +0x48), which shows a "View Tutorial" button; clear both for other uses. The Options icon (two cogs) is 0x520DE4E3 (pause) / 0x3058FE37 (main menu).
- Pause menu: `PauseMenu::SetupOptions` 0x52B4D0 builds the row with `IconScrollerMenu::AddOption` 0x573960 (`new pm_X(texture hash, name hash, description hash)`, `IconOption` ctor 0x586FA0).
  The tuning screen is `CustomTuningScreen` (7 `TuningSlider` rows; min -1, max 1, step 0.2).
- **Slider rows** [tested]: Audio's volumes are `FESliderWidget`s (ctor 0x589540, vtable 0x8A1490 = the toggle's 15 slots + [15] `SetInitialValues`; a `cSlider` at +0x5C, `fVertOffset` +0x98), added with `UIWidgetMenu::AddSliderOption` 0x588860,
  which looks the bar up in the layout as `BASE_SLIDER_n` / `FILLBAR_SLIDER_n` (n = the row's index; also `HANDLE_`, `LEFT_CAP_`, `RIGHT_CAP_`, `VALUE_`, which the Player / Video layouts lack, and the game copes) and calls `SetInitialValues`.
  All four Options layouts (`Options.fng`, `OptionsPCDisplay.fng`, `Pause_Options.fng`, `Pause_PCDisplayOptions.fng`) have the bars for every slot, so a page can mix toggle and slider rows. `SetSliderValues` 0x50E660 (min, max, inc, cur; bar range 160 px), `SetValue` 0x50E6A0, `DrawSlider` 0x50E6B0.
  Our slider rows replace Act (the shared `value_row_act`), Draw and `SetInitialValues`; a windowed page keeps value rows, since its widgets change which row they show.

## Dialogs (custom_menus `skip_dialogs.cpp`)
- Message boxes: every `DialogInterface::Show*` ends in `ShowDialog(feDialogConfig*)` 0x5983C0 (cdecl) [read]. Config (PC = decomp layout): formatted blurb +0 (512 chars), then
  {text hash, message} per button at +0x204 / +0x20C / +0x214, parent package +0x224, dialog package +0x228, button count +0x22C. A pressed button's message reaches the parent from
  `~feDialogScreen`: `cFEng::QueueGameMessage(msg, parent, port)` 0x516C50. Yes/No labels 0x70E01038 / 0x417B25E4; Yes 0xD05FC3A3, No 0x34DC1BCF [read].
  Callers (blurb hash): `CustomizeShoppingCart` 0x71D9E710 (career: 0x8EBAA44B, backroom 0x04810898, both "purchase and apply"), `CustomizeSub` 0x892CB612 (Yes 0x6820E23E),
  `CResumeFreeRoam::React` 0xEB694C0C, `UISafehouseRaceSheet` / `UIQRChallengeSeries` 0x77CF03C5 [read].
- The save system (RealCore memcard) has its own texts, `MEMCARD/LOCALE_<language>.loc` (UTF-16; ids from 1: 3 Yes, 4 No, 7 overwrite?, 8 Save complete., 10 Load complete.),
  `MemoryCard::GetLocaleString(id)` 0x518380. It asks through `MemcardCallbacks::ShowMessage(text, count, buttons)` 0x560980 (thiscall; sets `MemoryCard`(*0x91CB20)+0x14);
  the answer is `MemoryCard::MessageDone(button from 1)` 0x518410, which posts it to the save system's queue (handled on its next `Update`) [read].
- The save screen (`UIMemcardBase`) has its own prompts: `ShowYesNo(text, prompt id)` 0x543CC0 keeps the id in the flags at 0x91CBAC (mask 0x0F000000); `HandleButtonPressed` 0x563380
  moves it to 0x91CBCC and continues `DoSaveFlow(step)` 0x544370. "Save the alias named %s?" is prompt 4 from `SetupPromptSaveConfirm` 0x543EB0; Yes = `DoSaveFlow(12)` [read].

## Main menu (custom_menus `resume_career.cpp`)
- `UIMain` (`MainMenu.fng`): `Setup` adds MainCareer (icon 0x03704F3D, name 0x5815A2B5 "Career"), Challenge, Quick Race, Customize, Alias Manager, Options with `AddOption`. `MainCareer::React` 0x51AC80 only sets a game mode
  bit in `FEDatabase+0x12C` (0x100 career manager, 1 career if no memory card); the exit then switches to `MainMenu_Sub.fng` (`uiCareerManager`: Resume / Start New / Load Career) [read].
- `CResumeCareer::React` 0x532650 (5 stack args, does not use `this`): joyport from param 1, `CareerSettings::ResumeCareer` 0x57F570 (sets mode career, clears career manager), then queues `IG_SafehouseMain.fng` [read].
  CareerSettings = `*(FEDatabase+0x10) + 0xA8`; flags at +4: bit 0 `HasCareerStarted`, bit 11 `IsGameOver`. `bProfileLoaded` = FEDatabase+3. The last profile is loaded after the menu appears (`MemcardEnter` 0xF1 on INITCOMPLETE),
  so the label is updated in `UIMain::UpdateProfileData` 0x528BC0 (also run on message 0x7E998E5E when a profile loads). `IconOption`: icon +0xC, name hash +0x2C, description +0x30.
- Our version: `MainCareer::React` calls `CResumeCareer::React` when a career can be resumed, and the `MainMenu_Sub.fng` switch the screen's exit then makes is dropped [read; not run in the game].
- The safe house is `uiCareerCrib` (`MainMenu_Sub.fng` in career mode; `NotificationMessage` 0x531EA0). Its Back exit (EXITCOMPLETE with the previous button BACK) sets career manager mode and switches to `MainMenu_Sub.fng` again, which builds
  `uiCareerManager`; ours redirects that one switch to `MainMenu.fng` [read; not run in the game].
- The main menu's Options layouts (`Options.fng`, `OptionsPCDisplay.fng`, from `FrontB.lzc`: a JDLZ stream of chunks, each 0x30210 chunk a JDLZ-compressed FNG) have one set of hints, each a group of {label string, backing image, hit-box image, key glyph}:
  Defaults 0xD6463100, Accept 0x73DCB662, Back 0xC0A32823 (the pause layouts: 0x3FC72F9D, 0x9502D148, 0x523250E9); the Defaults glyph is `PC_KEY_3` 0x68A29A8C. `OptionsPCDisplay.fng`'s "Basic" hint is the group 0x56E65DA8 [read].

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
- The Help key opens `ShowOneButton` 0x5989B0. Its dialog layout is `InGameDialog.fng` in the pause menu and `Dialog.fng` in the main menu (`UIOptionsScreen::mCalledFromPauseMenu` is the byte at +0x124); the pause one is not loaded in the main menu, and asking for it froze the UI [tested].

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
- **Late "movie finished"** [tested]: with the byte set, a movie screen gets `0xc3960eb9` more than once - the switch to the next screen is only queued, the old screen lives on a frame or two, and each late
  message runs `MovieScreen::NotificationMessage` 0x57DDA0 -> `ChangeToNextBootFlowScreen` again (seen: six advances in two frames, all from 0x57DE71), skipping the splash screen and `MC_Main.fng`: a main menu with no
  profile and no 3D car viewer. `dev_tools` passes `0xc3960eb9` on only while the screen's package (MenuScreen +0x10) is the current boot screen.
- **Skipping the splash screen** [tested]: `MW_LS_Splash.fng` ("click to continue", `SplashScreen`) takes ~4 s to post `FEHASH_INITCOMPLETE` 0x35F8620B (it loads the game's resources meanwhile); its `NotificationMessage` 0x5A32A0
  then sets `bAllowContinue` (+0x2C) and advances (`ChangeToNextBootFlowScreen(0xff)`) on `__PAD_ACCEPT__` / `__PAD_START__`. Making that call on `INITCOMPLETE` works; `INITCOMPLETE` is posted more than once (also on `UIMain`),
  so only on the first.
- **`MC_Bootup` / `MC_Main`** [read]: `MC_Bootup.fng` (`UIMemcardBoot`, decomp `uiMemcard.cpp`) starts `MemoryCard::BootupCheck` 0x518440 on `INITCOMPLETE` and moves on when the check reports back
  (`MemcardCallbacks::BootupCheckDone` 0x559AC0 queues 0x461A18EE); a check can stop on a message that waits for an answer (`MemcardCallbacks::ShowMessage` 0x560980). `MC_Main.fng` (`UIMemcardMain`) loads the profile - the only
  one on its own, else a list - and ends with "Load complete." [Continue] [tested]. The choice is `UIMemcardMain::ListDone` 0x563E90: for the boot flow (`gMemcardSetup` 0x91CBAC, op & 0xF0 == 0x20; in-boot-flow byte +0x30) with one save it copies the save's name (list at `this`+0x7C, node name at +8 after `MemoryCard::GetPrefixLength` 0x518350 characters) to `this`+0x40 and calls `MemoryCard::Load` 0x5189F0; doing the same for a chosen save among several loads that one [tested]. MemoryCard (`*0x91CB20`, PC): waiting for an answer +0x14 (byte), operation +0x40 (1 boot-up check, 5 load), last error +0x48.
- **The tail destroys the manager** [tested]: a `ChangeToNextBootFlowScreen` / `JumpToScreen` that reaches `MainMenu.fng` calls `Destroy`, deleting the `BootFlowManager` inside the call: a hook must not read the screen through
  `this` after the original returns (that crashed); read `mInstance` again (null once destroyed). PC layout: `CurrentScreen` +0xC, `BootFlowScreen` name +0xC.

## Skipping the frontend (SkipFE) and loading a profile directly
- **main()'s SkipFE switch** [read]: `main` 0x666590, right after `InitializeEverything` 0x665FC0, checks the dword at 0x926064: if set, `RaceStarter::StartSkipFERace` 0x57ECD0 runs instead of `GameFlowManager::LoadFrontend`
  0x6645C0. It only copies race settings into gameplay globals (track 0x8F86A4, laps 0x8F86BC, player/AI car counts 0x8F86B8 / 0x926080, direction 0x926084, race type 0x926098, cops off 0x8F86C0, max cops 0x926088, traffic 0x926090
  (float), difficulty 0x8F86CC, POV 0x8F86C4, split screen 0x92607C, car names as string pointers at 0x8F86A8 / 0x8F86AC); it does not start a race. Same addresses as xan1242's `xnfs-skipfe`. Set from an ASI plugin, the byte
  usually lands after `main()` has checked it. With it set and no profile, the game once saved a profile literally named `NAME` (a text field's placeholder) [tested].
- **`QuickGame::OnBeginState` 0x6F4EA0 also reads it** [tested]: on any `QuickGame`'s first state (a Quick Race from the menu included) `HandleSkipFEOptions` 0x6EA040, if SkipFE is set, no startup race is queued yet
  (`GRaceDatabase::GetStartupRace` 0x5DCA00) and the race ID at 0x926068 (11 characters) is not empty, builds a race from that ID (`StringHash32` 0x4519D0 -> `GRaceDatabase::GetRaceFromHash` 0x5FB710) with the settings above
  and calls `SetStartupRace`; `sub_741690` likewise replaces the car. Race IDs look like `"5.1.1"` (decomp `uiQRTrackSelect.cpp` `IsRaceValidForMike`; `GRaceDatabase.cpp` `sDDayRaces` are the scripted D-Day races).
- **Loading a profile with `cFrontendDatabase::LoadUserProfileFromBuffer` 0x5ACDD0 does not work** [tested]: reading `<Documents>\NFS Most Wanted\<name>\<name>` and passing it in (thiscall: buffer, size, player; `*0x91CF90`)
  failed `UserProfile::LoadFromBuffer` 0x5ACB70's version check against `*[FEDatabase+8]`, and failing still emptied the car stable (crashes later). The Load Game screens are the way in.

## Camera presets (camera_settings)
- `CameraAnchor::GetPov` 0x4713C0 (thiscall, 1 arg = preset 0..6, returns POV*) runs every unpaused frame (~110/s) from `CubicCameraMover::Update` and rebuilds the POV (at anchor+0xC8 on PC) from the car's
  `camerainfo`; hooking it and overwriting the result is the override point. POV layout: type@0 s16, angle@2 u16, lag@4, height@8, lat@0xC, fov@0x10 u16, stiffness@0x14, tilting@0x18.
  `GameFlowManager::IsPaused` 0x64B680 (ecx 0x925e70).
- `CameraAnchor` (PC) [read]: the car's velocity is at +0x00..0x08 and its magnitude (m/s) at +0x10, written by `CameraAnchor::Update` 0x468FA0 each frame; `mZoom` +0xC4, camerainfo `Instance` +0x100. The decomp's offsets from +0xB4 on are 0x10 too high.
- **Row slots of the Options layouts** [tested]: each widget takes the layout's `OPTION_NAME_n` string as its title object (`widget+0x2C`); a widget past the layout's last has none and `FEToggleWidget::SetFocus` 0x574630 crashes on it
  (UP at the top of a 12-row page wraps to the last row). `Pause_Options.fng` (the Player screen) has 10 slots and **no scroll bar objects**. `Pause_PCDisplayOptions.fng` / `OptionsPCDisplay.fng` (the PC Video / "Advanced Video" screen, category 1, also a
  `UIOptionsScreen`: creator 0x570A80 for all of them, `SetupVideo` 0x5295F0 fills it) has 15 slots, shows 10, and has the scroll bar (two 5 px images, colour (64, 174, 255), track alpha 60, handle 200) and the up / down chevron groups with their click responses
  (`7eabca56: (2 PAD_UP|PAD_DOWN) (1 <flash event> fffffffc)`) in the layout; `UIWidgetMenu` drives it all (`FEScrollBar` at +0x54: `Update(rows in view, rows, first row, selected row)` 0x589870, called by the game's own scrolling with the widget count). So a page with more than
  10 rows is built on the Video screen instead of the Player screen (set the category to 1 and open `Pause_PCDisplayOptions.fng` arg 1 / `OptionsPCDisplay.fng` arg 0; hook `SetupVideo` to replace its rows). The Video screen's own code must not run for it: with the display
  settings "changed" the Accept key (0x545831) and leaving (0x545B47) apply them (`sub_510300`) and ask "keep these display settings?", and discarding restores them, so `OptionsDidNotChange` says "unchanged" at those two call sites and the original `RestoreOriginals` is skipped;
  leaving does not mark the profile's options dirty (`FEDatabase+4`) on that screen, so we do. Past the 15 slots a page shows a window of its rows (the widgets are the slots, `kRowIndex` = slot; up / down at the window's ends slide it, `handle_window_scroll`; the bar is updated with
  the page's rows after each press).
- **Previous Camera View** (`camera_settings/previous_camera.cpp`) [read]: binding entry 67 (SC_NUMPAD5; nothing in the data refers to it) is renamed `CAM_ACTION_PREVIOUS` in both tables, bound to nothing, and
  its bindings kept in our ini (`[previous_camera] binding`; saved when the Controls screen refreshes and the live table holds controller 0's set). Its row goes under the game's Change Camera View (row 15) through
  nfsmw_custom_menus, which also replaces that row's text (the language string keyed bStringHash("CAM_ACTION_CHANGE")) with "Next Camera View". The action: `InputPlayer::FetchInput` 0x6A8870 is hooked; the entry's value
  is read from the player's device (`IOModule` 0x920DE0: +4 count, +8 devices, indexed by the player's controller port; a GameDevice has vtable 0x8A7B9C and its values at +0x20) and a rising edge calls
  `PlayerSettings::ScrollDriveCam(settings, -1)`. FetchInput only runs for a human player in a race, so a press in a menu does nothing.
- **A camera with no race** [read; not run in the game]: the `CameraAnchor` constructor 0x4716A0 (thiscall, 1 arg = car type name hash, `ret 4`; the decomp calls it `__ct__12CameraAnchori`) builds the anchor from zeroed memory (0x200 bytes
  is enough: the `camerainfo` Instance at +0x100 is the last member), `SetModel` 0x471370 finds the `CarTypeInfo` by that hash (`GetCarTypeInfoFromHash` 0x739940; array `*0x9B09D8`, 84 x 0xD0 bytes, name +0, hash +0x50) and picks the
  `ecar` collection (class 0xA5B543B7) by the lower-cased name; `GetPov` then works on that anchor with mZoom 1.0 and speed 0. Nothing in the ctor registers the anchor anywhere. The Camera page uses it in the main menu, for the car "bmwm3gtr" (the hero car; "bmwm3gtre46" is another).
  Of a preset's `camerainfo` pairs `GetPov` takes the one picked by `eGetCurrentViewMode() == 3`.
- `GLOBAL/attributes.bin` (VPAK) holds at least 81 `camerainfo` collections (class definition at 0x2b1a0: FOV 0x263E9452, HEIGHT 0x762B7718, STIFFNESS 0x7F8EEA1A, LAG 0xDE0857E3; collections from
  0x2b230, 6 float pairs each); they differ per car and view, and the blend between a pair is not worked out. So the page stores **offsets** from the driven car's value, added in the GetPov hook.

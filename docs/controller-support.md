# Controllers, Controls menu, button prompts, rumble (nfsmw_controller_support)

Binding table, devices and queues: see [input-system.md](input-system.md). Marks: [read] [tested] [guess].

## Controller state
- The game polls a controller in `sub_7F28A4` (GameDevice's DirectInput controller): `call [ecx+0x24]` = GetDeviceState (0x110 bytes) into a DIJOYSTATE2 at
  `ebp-0x13C`; the instruction after it, `0x7F2927 mov eax,[esi+0x464]`, is where `process_pad_state` is called (dead zones, triggers as two sliders, "last input was a
  pad", menu hotkeys) [tested]. **Steam Input's virtual controller** (vendor 0x28DE, product 0x11FF, shows as "Controller (XBOX 360 For Windows)") does not go through
  dinput8's own `GetDeviceState`, so a hook on that never sees it; the game-level hook does. The widescreen fix hooks the same function further on (`0x7F2AE2`,
  writes the hotkey table) and, in its gamepad mode, rewrites FE Accept/Cancel/Defaults bindings on every read; the widescreen fix's own ini has `[CAMERA] Enable = 0` and dead zones 0.
- XInput as DirectInput shows it: sticks 0..65535 with Y down, both triggers on Z (we hold Z centred and put the triggers on sliders 1 / 2), buttons A B X Y LB RB Back Start LS RS,
  D-pad as hat 0. The k-th XInput game controller is the k-th connected XInput slot (`own_slot`) [tested: rumble pulse on the selected controller].
- **InputMapping** (ctor 0x6479D0): entries `{type, lowerDZ, upperDZ, action, scalar, prev, current}`; type 0 analog through the dead zones, 1/2 press / release;
  actions 1 steer left, 2 steer right, 5 gas, 6 brake. We turn steering / gas / brake entries analog.
- **Mouse**: the vanilla game never reads the mouse through DirectInput. It is Win32: `__UpdateCursorPos` 0x6C9B60 (`GetCursorPos`, HWND at `[0x982BF4]`) feeds the FE mouse
  object at 0x987F50 (x/y `+4/+8`, buttons `+0x20/+0x23`); the OS cursor is hidden once at window setup, the pointer drawn is an FE object. The widescreen fix supplies a
  DirectInput mouse on the copy that has it installed; without it we make our own (`poll_own_mouse`, a hand-built DIMOUSESTATE format, shared, foreground).

## Controls menu (UIOptionsPCController)
- Rows are binding entries by index: 0-17 keyboard list, 0-18 controller list; 4 look rows (entries 25-28, once SC_W/A/S/D which no input mapping refers to) are appended; other mods can add rows too (Previous Camera View, entry 67, under the game's Change Camera View: [menus-and-camera.md](menus-and-camera.md)).
  Patches (`custom_menus/controls_rows.cpp`, applied the first time the Options menu opens, for the rows the mods register; the entries stay with the mod that owns them): object size 0x2B4 to 0x32C (`push` at 0x571D16), its byte field +0x2B0 to +0x328 (0x52ACA1, 0x51D56D, 0x51D645), row count `add r,0x12` to 0x17 at
  0x52ACE8, 0x52AE1D, 0x51D42E, 0x51D498, 0x51D4E7, 0x51D555, 0x51D5C7, 0x51D636. Row to entry translation in 0x628CB0 (only when called from the Controls screen code: the
  profile save/load 0x56E180 / 0x56E200 uses it for entries 0-18), 0x627BB0 (name), 0x628230 (label), 0x628560 (clear), 0x628C00 (start remap). A row's name is the language
  string keyed bStringHash(entry name), merged into the language table.
- Row layout: the game's rows, with each added row placed after its `after_row` (or at the end), as a row -> entry table per list; the hooks below read it.
- The screen keeps a copy of each row's two bindings (0x18 bytes per row from +0xE8) to detect and undo changes.
- Look camera: `CubicCameraMover::Update` 0x47D3C0 hooked (mover `+0x9C` view/mode, `+0xA1` looking back), the look-at call at 0x47DCBC redirected; driven by entries 25-28.
- The title icon fix lives in `custom_menus/title_icon.cpp` (hook on `UIOptionsPCController::Setup` 0x52A7B0).
- The game has a generic scrolled list (`ArrayScroller` / `ArrayScrollerMenu`, ctor 0x593090 / 0x5938A0, `AddSlot` 0x5881F0, `AddDatum` 0x59BD30) used by the Controls screen,
  rap sheets, milestones, My Cars; our Options pages use `UIOptionsScreen`'s fixed rows (at most 40). Unexplored: a page on the scroller for unlimited rows.

## Default controller bindings
- **`sub_63CD80`** (the controller defaults, per controller model by the hash of the joystick's name at `[0x91F150]+0x114`) [read] is called by `GameDevice::InitInputs` (start-up, tail jumps), by the Controls screen's Defaults (0x51D702) and by the profile
  load `UserProfile::BindingsIO_B` 0x56E200 (0x56E27D) when the profile's saved controller type differs from the connected one. It rewrites the controller slots of the live table (and, as far as we saw, clears those of entries it does not know: our look entries
  needed theirs put back). The default table 0x8F6D80 has controller slots only for the menu entries. So: start-up defaults come first, then the profile's saved entries 0-18 replace them; Defaults and a profile of another controller type run 63CD80 again.
- `default_layout.cpp` writes our layout (slot 2, slot 3 cleared) into the default table at start-up, into the live table at the first poll (after InitInputs) and, through `nfsmw_custom_menus` (`add_controller_defaults`, which hooks 63CD80), after every later reset.
  Entry names for the Controls rows [read from English.bin]: 11 View Blacklist, 12 View World Map, 13 View SMS Messages (the HUD pad entries), 15 Change Camera View, 16 Look Behind, 17 Pull Camera Back.

## Rumble from the Controls screen
- The Controls screen's "Rumble: On/Off" hint (language string 0x3DF9961B, controller column only) is the message 0x36D64627 in `UIOptionsPCController::NotificationMessage` 0x545F70 [read]: it flips the profile's Rumble byte (options +0x84) and redraws the hint (`this+0x2C`).
  We turn it into the right shoulder button's message (0xD9FEEC59), whose path asks "Do you wish to keep the modified settings?" (messages 0xB850B019 yes / 0x6DC9631 no, 0x34DC1BCF cancel) when the bindings changed, and ends in `SwitchDevice` 0x52AF20; our hook there opens the page instead.
- Opening an Options screen from another screen [read]: `UIOptionsMain::NotificationMessage` selects the category (`options+0x24`; Player = 4) and calls `cFEng::QueuePackageSwitch(pkg, arg, 0, false)` 0x525940 with `Pause_Options.fng`, arg 1 (pause menu layout: in game and bits 3 and 6 of FEDatabase+0x12C clear) or `Options.fng`, arg 0.
  The Controls screens are `Pause_PC_Controller.fng` (arg 1) and `Options_PC_Controller.fng` (arg 0). `UIOptionsScreen::NotificationMessage` 0x545750 leaves on FEHASH_EXITCOMPLETE 0xE1FDE1D1: it updates the "options changed" flag (FEDatabase+4) and then switches to `Pause_Main.fng` / `OL_MAIN.fng` / `MainMenu_Sub.fng`,
  so a mod that wants the page to return elsewhere replaces that last switch (nfsmw_custom_menus does, `open_page`).
- Rumble levels are per XInput controller (slot 0-3): `[rumble_1]`..`[rumble_4]` in the ini, falling back to `[rumble]` for effects a controller has no key for.

## Button prompts and menu hotkeys
- Prompt pictures: hook `FEngine::Update` 0x5C53C0, walk the packages (`+0xE0`, next at `+4`) and `FEPackage::ForAllObjects` 0x5B1560 with a callback object (first member
  points to `{destructor, function}`); an FEImage (type 1 at +0x18) whose texture (+0x24) is one of the 14 PC / `XBOX_` / `PS_` pairs gets the right one, `flags(+0x1C) |= 0x2400000`.
  Which: `last_input_was_pad()`. Texture names are cut at 23 characters in the packs (the hash of the full name is what objects use: the zoom keys are
  `PC_GREATERTHAN_LESSTHAN_KEYS`, hash 0x7058EAE2). Pictures: `controller_support/buttons/`, `prompts.ini`, built by `Tools/button_pack.py`.
- Menu layout: entries 32 FE_ACTION_ACCEPT, 33 CANCEL, 41 FE_ACTION_B5 on controller slot 2: button objects 0 / 1 / 3 (A, B, Y) in the default table 0x8F6D80 and the live one.
- Hotkeys "2" / "3" / "4" (Advanced Options, Showcase, Movie) and Quit (Q) are keys, not binding entries: key states are dwords in a table at 0x8F3448 (lookup 0x514B90 ORs
  four neighbours, 0x1C bytes apart): "4" at +0xB0, "3" at +0xCC, "2" at +0x13C.

## Rumble
- The PC pad gets a feedback object but all its methods are empty (GameDevice's `IFeedback` at +0x2C, vtable 0x8A7BD4; only PauseEffects 0x628800 does anything). We put our object in
  front of the player's (`SetControllerPort` hook; `DoFFB` 0x6EE490 view: feedback `+0x58`, wheel `+0x5C`; `SetControllerPort` view: `+0xC` / `+0x10`).
- Per frame `DoFFB` calls: `CanDoFFB` (profile Rumble = `PlayerSettings`+4; `bRumbleEnabled` at 0x8F86E0; not paused, not AI, not animating) or only PauseEffects; per wheel
  TireSlip, TireSkid, RoadNoise with the wheel's surface (the `null` surface in the air, key 0xD929E923 with a blown tire); RPM(powerband, overrev always 0, throttle),
  ShiftPotential, NOS(engaged, capacity); EngineBlown for cars with `IEngineDamage` (the player's car has it) else Shifting.
- The PC build's `CanDoFFB` fetches the settings (`LocalPlayer::GetSettings` 0x6E99A0, this = the view at `+0x4C` of DoFFB's) but never checks the Rumble flag (`PlayerSettings`+4; only `OnCollision` does), so `DoFFB` keeps feeding the feedback object with the profile's Vibration off: we read the flag ourselves [read].
- **Gear changes** are not reported to cars with `IEngineDamage`: read the gear ourselves. `GetSimable` is method 3 of the IEntity at `+0x2C`, `QueryInterface` 0x5D59F0 (thiscall,
  `ret 4`) with the `ITransmission` handle 0x404010 (GetGear 1, IsGearChanging 4, GetShiftPotential 11); `GearID` 0 reverse, 1 neutral, 2 first.
- **Steering wheel**: if `mWheelDevice` is set and `IsConnected` (0x628E10: only asks whether the port has a DirectInput joystick, so an Xbox pad counts) `DoFFB` calls the wheel's
  `UpdateForces` (0x6430D0) and returns without touching the feedback object; we hide the wheel during `DoFFB` when all DirectInput joysticks seen are XInput (it stays elsewhere:
  `SuspensionRacer::CalculateMaxSteering` asks it for the steering type).
- Collisions arrive in `LocalPlayer::OnCollision` 0x6F2370. `Sim::Collision::Info`: type bits 0-2 at +0x1C (1 object, 2 world, 3 ground), sliding bit 7, closingVel +0x20, force +0x2C,
  impulseA +0x5C / impulseB +0x6C (each body's change of speed, m/s), slidingVel +0x70. IEngineDamage = the drag race's overheating engine and the opening race's sabotaged car; spike strips blow tires.
- Surface layout: `RenderNoise` {Hz, degrees, min speed, max speed} at +0xCC, name +0xDC, `WheelEffectFrequency` u16 +0xF8, `WheelEffectIntensity` u8 +0xFA. Measured: wood 4 Hz / 1 degree /
  full at 12 m/s, effect 120/100; grass 10 Hz / full at 50 m/s, 40/80; gravel 10 Hz / 50 m/s, 60/110; asphalt, concrete, null: none.
- Measured feel: wall / car hits change speed 2-13 m/s (props 0.5-1); tire skid (lateral speed) 2-5 m/s in corners, 8-11 in drifts; wheelspin 5-10 m/s at a launch, 34 in a burnout;
  cruising revs 0.7-0.9 of the powerband (a constant 6-12% on the heavy motor hid the events, hence per-effect multipliers).
- Motors: the light motor is much weaker than the heavy one and on/off chatter faster than about 8 Hz blurs (spin-up + Bluetooth latency): effects use both motors with smooth pulses of
  at most 8 Hz; a level past one motor's maximum spills half its excess into the other. A steady level is re-sent every 0.5 s (Bluetooth pads stop otherwise); changes go out at most
  every 30 ms, rises of 10% and stopping at once. The rumble stops 250 ms after the game stops updating and when the game loses focus.
- Reference: the decomp's `Misc/Rumble.cpp` has an older envelope-based rumble (crash envelopes 0.8 s and 1.2 s).

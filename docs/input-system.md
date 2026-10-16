# Input system (PC)

Functions are named in `Tools/re/names.json` / the decomp (use `Tools/re/re.py name <addr>`); this page has the data and the flow.
Marks: [read] disassembly, [tested] confirmed in game or by a crash, [guess].

## Devices and polling
- **IOModule** (global 0x920DE0) [read]: `+0` enabled, `+4` device count, `+8` device pointers (16), `+0x48` bit mask, `+0x4C` last change.
  `IOModule::Initialize` fills it from registered factories: GameDevice (port 0), MouseDevice, KeyboardDevice (on this PC only the first ended up in it:
  a device added later got index 1 [tested]). Each frame `IOModule::Update` → `PollDevices` (vtable[4] of every device) → `UpdateAllDevices`
  (every ActionQueue reads `devices[its port]`).
- **GameDevice** (0x1858 bytes, vtable 0x8A7B9C) [read]: `+0x24` port, `+0x18` input records (256 × 16 bytes from `+0x34`; a record is
  `{1, name hash, index, float* into current}`), `+0x1C` previous / `+0x20` current values (0x400 bytes each), `+0x1838` record count, `+0x184C` a component.
  Only the **first** device gets its tables from the constructor, and `InitInputs` / `PollDevice` do nothing for port != 0.
- **Joystick discovery** [read, tested: connecting controllers while the game runs works]: `GameDevice::GlobalInit` makes the DirectInput wrapper (`InputManager`, 0x9203B4; `+0xB4` the IDirectInput8) whose constructor enumerates every
  attached device once with `0x7F1A6B`. For a device of type 0x14-0x18 that callback makes a 0x4A4-byte joystick object (ctor 0x7F2735: DirectInput device at `+0x468`, data format, registers itself with
  the manager, which polls its devices every frame), numbers it, and passes it to the callback of GlobalInit (0x6283D0), which appends it to the joystick table (0x91F150, count 0x9203B8). Nothing
  enumerates again, so a controller connected later is unknown. `new_controllers.cpp` runs `0x7F1A6B` itself for an attached game controller the table does not have. For the first controller
  it also does what `GameDevice::InitInputs` does for joystick 0: its resting state into 0x91F1E8 (the Controls screen's rebinding compares with it), then `0x63CD80` (the defaults of its model); then the profile's
  saved bindings (`UserProfile::BindingsIO_B` 0x56E200, copy kept by a hook on it) go over those again, as the profile load does at start-up. FEngine's pad count is a constant 1 (0x51692F). The Controls screen builds with its device elements (object 0x8A41F5B9) hidden (script HIDE 0x16A259) when there is no joystick; `controllers_changed` in `pad_sets.cpp` shows them again. DirectInput does not say which XInput slot a device is, and its list order is not the slot order (a slot is the first free one): `input.cpp` pairs them when a device's buttons match exactly one slot's (`learn_slot`).
- **Extra device** [tested]: a second GameDevice from `GameDevice::Create`, tables and records set up by hand, appended to the IOModule, filled each frame in
  `PollDevice`'s hook (`Mods/controller_support/extra_device.cpp`). Gives a second player an input path of the game's own.

## Bindings
- **Binding table** (0x91F418) [read, tested]: 76 entries × 0x34 bytes: `{name*, 4 × slot}`, slot = `{type, device, object}`. Slots 0/1 keyboard, 2/3 controller.
  Types: 1 key (device = DIK code), 2 button, 3 hat (0 up 1 right 2 down 3 left), 4/5 axis towards 0 / towards 65535 (object 0 X, 1 Y, 2 Z), 6/7 rotation
  axes, 8/9 sliders. Default table 0x8F6D80. The profile saves entries 0-18; 25-28 are ours (camera look).
  Entries: 0 GAS, 1 BRAKE, 2 STEERLEFT, 3 STEERRIGHT, 4 HANDBRAKE, 5 GAMEBREAKER, 6 NOS, 7 SHIFTDOWN, 8 SHIFTUP, 9 RESET, 10 HUD ENGAGE_EVENT,
  11-13 HUD PAD DOWN/LEFT/RIGHT, 14 FE_ACTION_B2, 15 CAM_ACTION_CHANGE, 16 CAM_ACTION_LOOKBACK, 17 CAM_ACTION_PULLBACK, 18 HUD PAUSEREQUEST,
  19-22 FE LEFT/RIGHT/UP/DOWN, 32 FE ACCEPT, 33 CANCEL, 34 START.
  Entries whose names no data refers to (no hash hit in `GLOBAL/attributes.bin`): 25-28 SC_W/A/S/D (our look), 42 / 43 DEBUG_ACTION_TOGGLECAM / CARCOLOR, 58 SC_RETURN, 67 SC_NUMPAD5 (nfsmw_camera_settings: Previous Camera View).
- An entry's value is the strongest of its 4 slots (`BindingValue`, called only by `PollDevice`).
- **Joystick table** (0x91F150) [read]: 8 pointers, one object per DirectInput game controller in enumeration order; the object has the DirectInput device at
  `+0x468`, the name at `+0x114`, state arrays at `+0x218` / `+0x220`. A slot's `device` indexes this table. Keyboard object 0x91F1D8, mouse 0x91F1E0.
- **Rebinding** always records `{type, 0, object}`: the pressed controller is ignored [read, tested]. Vanilla has bindings for controller 0 only;
  `pad_sets.cpp` keeps a set per controller.

## Who may drive (our policy)
`PollDevice`'s hook (`hook_poll`) takes the entries the game computed (every device's slots), adds the other controllers' sets (`add_values`), and then, when another mod named a
vehicle source (`vehicle_source`: split screen), replaces the **vehicle entries** (0-13 gas...HUD pad, 15-18 camera / pause request, 25-28 look) with what that device alone asks for
(`restrict_vehicle_entries`). Entries 14 and 19 up (menus, hotkeys) always come from every device, so a menu is never tied to one device. The DirectInput states of all devices are never blocked
(the Controls screen's rebind capture reads them directly).

## Queues and players
- **ActionQueue** [read]: `+0x268` port, `+0x26C` has entries, `+0x270` entries, `+0x280/0x281` flags. A queue reads `IOModule.devices[port]`.
- **InputPlayer::FetchInput** [read]: sets the queue's port from `player->GetControllerPort()`, then pops actions: steer `+0x2A0/0x2A4`, gas `+0x2A8`,
  brake `+0x2AC`, look back `+0x2B0`; camera change calls `PlayerSettings::ScrollDriveCam(settings, 1)` 0x5A3850 (-1 scrolls back; `CurCam` at +0xC of the settings). In `FetchInput`, the owner is at `this-0x40`; its vtable +0x20 is the player, whose +0x14 is the settings and +0x34 the controller port [read].
- **LocalPlayer**: controller port `+0x18`, feedback `+0xC`, wheel `+0x10` (taken from `devices[port]` by `SetControllerPort`).

## FEDatabase (0x91CF90) joystick ports
`+0x0C` / `+0x0D`: player 1 / 2 (signed byte, -1 none). **The menus' input (`cFEngJoyInput`) has one queue (`+0`, "all ports") and leaves the slots for ports 1-3
uninitialised on PC**; `CheckUnplugged` and `JoyEnable` index them with these ports, so a port of 1-3 in the database crashes [tested]. Player 2's database
port stays -1; their *LocalPlayer* gets the extra device's port instead (`SetControllerPort` hook).

## Controls screen (UIOptionsPCController)
`+0xE4` column (1 keyboard, 3 controller). The shoulder buttons are the messages PAD_LTRIGGER 0x5073EF13 / PAD_RTRIGGER 0xD9FEEC59, both ending in
`SwitchDevice` (`0x6DC9631` is only a dialog answer) [tested]. The header's device name comes from `GetDeviceName`. Row patches: `controls_menu.cpp`.

## Open
- What the pause screen of a second player does with the player's controller port.
- Whether the IOModule mask (`+0x48`) matters for a device added later.

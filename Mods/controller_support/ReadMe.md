# Controller Support

*Disclaimer: The code was written by Claude (Sonnet & Opus). The custom menu icon, testing, design, and this page, were made by me, a human.*

This mod for Need for Speed: Most Wanted 2005 (1.3) gives full controller support including analogue steering and throttle, camera look while driving, input remapping, and customizable rumble. You should also grab [Custom Menus](../custom_menus/ReadMe.md) to get these rumble settings properly integrated in the game's pause menu. Otherwise, you have to hand-edit `scripts/mets_mods/nfsmw_controller_support.ini`.

## What it does

### Input
The game reads controllers through DirectInput, where an XInput controller has three problems. This plugin fixes them in
`IDirectInputDevice8::GetDeviceState` (only for XInput controllers, found by `IG_` in the device path) and in the game's
action mapping:

- **Analog steering, gas and brake**: The vanilla game maps them as on/off inputs, so even a stick bound to them was not analog. This mod enables analog throttle and reversing, however, the mod does not change the default keybinds - **you must set them to the triggers and the sticks yourself, in the Controls options menu**.
- **Triggers**: In the vanilla game, both triggers share one axis, so gas and brake on the triggers cancel each other. LT and RT now report
  as two separate axes (Slider 1 and 2) and bind like anything else, so they do not cancel each other.
- **Dead zones**: Currently the mod comes with hard-coded deadzones to address stick drift, customization for this is a TODO.
  They are analog now, with whatever you bound them to (stick, trigger, keys still work).

### Controls menu
- Direction names for controller bindings ("Left stick Left", "D-pad Up", "LT" instead of the widescreen fix's
  direction-less names).
- Four extra rows at the end of both lists: **Look Left / Right / Up / Down**, remappable like the rest.

### Camera look
All 3rd-person cameras can be controlled with either the mouse, or the inputs bound to the new "Look" keybindings which you can find in the Controls menu. Bound to the right stick by default. After letting go of the "Look" inputs for a few seconds, the camera returns to normal.

<TODO: Screenshot of new options.>

For this to work, the widescreen fix mod's camera look must be disabled; its `[CAMERA] Enable` must be set to 0.

### Rumble (Options > Rumble)
The vanilla game has force feedback events (road surface, tire slip and skid, engine revs, nitrous, gear changes, blown engine,
collisions) that were never connected to anything on PC. With this mod, they now drive the controller's rumble motors. If you have Custom Menus installed with this mod, you can customize the rumble strength of each event in a dedicated options menu. As you change a slider, the controller will rumble to show you how strong that setting feels.

<TODO: Screenshot, after mod is finalized.>

## Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- Recommended: [Custom Menus](../custom_menus/ReadMe.md) mod if you want to customize cameras in-game instead of digging in an .ini file.
- Recommended but **must be configured**: WideScreenFix: Set the following settings in that mod:
  - `ImproveGamepadSupport = 1` for better controller icons.
  - `LeftStickDeadzone = 0` and `RightStickDeadzone = 0` because this mod will handle deadzones.
  - `[CAMERA] Enable = 0` because our mod adds camera look to all third-person camera types instead of just two arbitrary ones.
- Incompatible: NFS-XtendedInput, potentially other similar mods that mess with controller functions.

## Limitations

- **XInput controllers only** (Xbox 360 / One / Series, and anything that looks like one: PlayStation and Switch pads
  through DS4Windows, Steam Input, ...). The plugin touches no other DirectInput device.
- **One player.** Only the first XInput controller and the game's port 0 are handled: no split-screen.
- **The look bindings are not in the game's profile**: the profile saves only the game's own 19 controls, so the four look
  rows are kept in `nfsmw_controller_support.ini` (shared by all profiles).

## TODOs
- Figure out whether input remapping code should live in this mod or be moved to the Custom Menus mod.
- Test other controllers through DS4Windows and SteamInput. Currently only an Xbox 1 controller was tested. I also own PS5 and Switch controllers.
- Make a custom icon for the rumble menu - but maybe it becomes a "Controller" menu instead, since we want to add more options than just Rumble:
- Add options to tweak deadzones of triggers and sticks from the menu
- Add option to switch to Playstation icons
- Become independent of WideScreenFix: Take their icons, improve the "A" button icon which is currently blue when it should be green.
- Test and support 2-players.

<details>

<summary>Code Notes</summary>

## Settings, files

In the folder `scripts/mets_mods/` (next to the `.asi` files): `nfsmw_controller_support.ini` and `nfsmw_controller_support.log`. All optional; the Options > Rumble page and the Controls menu
write the rumble levels and the look bindings for you.

```ini
[pad]
enabled=1                 ; 0 = the plugin does nothing
stick_deadzone=0.15       ; 0-1, left and right stick
right_stick_deadzone=0.15
split_triggers=1
analog_mapping=1
labels=1                  ; direction names in the Controls menu
[look]
enabled=1
stick_speed=150           ; degrees per second at full tilt
mouse_sensitivity=0.15    ; degrees per mouse count (0 = no mouse look)
invert_y=0
return_delay=3            ; seconds idle before the view returns behind the car
return_speed=2
min_pitch=-10
max_pitch=60
[rumble]
enabled=1                 ; read at start-up
collisions=100            ; the levels, 0-100; also reloaded while the game runs
landings=80
shifts=60
nos=70
drift=40
wheelspin=40
rev_limiter=40
road=40
engine=0
tire_blowout=80
engine_blown=80
[look_bindings]           ; written by the Controls menu: the four look rows' bindings
```


## Code layout

| File | What |
|---|---|
| `main.cpp` | the plugin entry: loads the settings, installs each part's hooks |
| `settings.cpp`, `pad.h` | `nfsmw_controller_support.ini`, the log, shared helpers |
| `input.cpp` | DirectInput state (dead zones, triggers, mouse deltas), analog action mapping |
| `bindings.cpp` | the PC binding table: the four look entries, a log of every binding change |
| `controls_menu.cpp` | the Controls menu: look rows, direction names |
| `look.cpp` | camera look |
| `rumble.cpp` | rumble: the force feedback object the game talks to, the effects, the motors thread |
| `rumble_page.cpp` | Options > Rumble, a page of nfsmw_custom_menus |

Each part has a header and its own namespace (`pad::input`, `pad::rumble`, ...); shared helpers are in
[`common/`](../common).

### How rumble works
`LocalPlayer::SetControllerPort` takes a feedback object from the player's input device. On PC that object (inside
`GameDevice`) does nothing, so a pad never rumbled. The plugin hooks `SetControllerPort` and puts its own object in
front of it: it passes every call on to the game's object, and turns the events into a level (0-1) for both motors.
`LocalPlayer::DoFFB`, which feeds the object every frame, also hands the frame to DirectInput wheel effects when it
thinks a wheel is connected, which it does for any joystick, so that call is hidden for XInput controllers. DoFFB
checks the Vibration setting only through a path the PC build left out, so the plugin checks it. A separate thread sends
the level to the controller (`XInputSetState`) at most every 30 ms. Findings, addresses and the measurements the strengths
were calibrated on are in [`notes.md`](../../notes.md).

</details>
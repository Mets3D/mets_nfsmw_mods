# Controller Support

*Disclaimer: Vibe coded with Claude. Thoroughly tested.*

The plugin `nfsmw_controller_support.asi` for Need for Speed: Most Wanted (2005, PC, `speed.exe` 1.3) for Xbox-style (XInput) controllers. It fixes the
game's controller input at the source, adds rumble that the PC game never had, and adds a free camera look. Every
binding stays in the game's own Controls menu. Its Options > Rumble page is built with
[nfsmw_custom_menus](../custom_menus/ReadMe.md).

## What it does

### Input
The game reads controllers through DirectInput, where an XInput controller has three problems. This plugin fixes them in
`IDirectInputDevice8::GetDeviceState` (only for XInput controllers, found by `IG_` in the device path) and in the game's
action mapping:

- **Triggers**: both triggers share one axis, so gas and brake on the triggers cancel each other. LT and RT now report
  as two separate axes (Slider 1 and 2) and bind like anything else.
- **Dead zones**: the sticks never rest exactly at the centre, which makes anything watching for stick movement (the
  widescreen fix's keyboard / controller icon switching) see constant input. A radial dead zone centres them exactly.
- **Analog steering, gas and brake**: the game maps them as on/off inputs, so even a stick bound to them was not analog.
  They are analog now, with whatever you bound them to (stick, trigger, keys still work).

### Controls menu
- Direction names for controller bindings ("Left stick Left", "D-pad Up", "LT" instead of the widescreen fix's
  direction-less names).
- Four extra rows at the end of both lists: **Look Left / Right / Up / Down**, remappable like the rest. The title icon of
  the screen (a game bug: it shows the audio icon) is fixed.

### Camera look
The chase cameras (close, far, super far and the like; not bumper or hood) turn with the look rows, bound to the
**right stick** by default, and with the **mouse**. After a few idle seconds of driving the view returns behind the car.
Replaces the widescreen fix's camera look (its `[CAMERA] Enable` must be 0: both change the same call in
`CubicCameraMover::Update`).

### Rumble (Options > Rumble)
The game has force feedback events (road surface, tire slip and skid, engine revs, nitrous, gear changes, blown engine,
collisions) that were never connected to anything on PC. They now drive the controller's motors. In **Pause > Options >
Rumble**: the game's own **Vibration** setting, then a level for each effect, 0 to 100:

| Effect | Default | What you feel |
|---|---|---|
| Collisions | 100 | a jolt, harder the more the hit slows your car; a grinding rumble (half level) while sliding along something |
| Landings | 80 | the car's body hitting the ground: a jump, bottoming out |
| Gear Shifts | 60 | a short kick on every upshift |
| Nitrous | 70 | a surge, then a steady push while it lasts |
| Drifting | 40 | tires sliding sideways, stronger the faster |
| Wheelspin | 40 | spinning wheels (burnouts, hard launches), locked brakes |
| Rev Limiter | 40 | full throttle at maximum revs |
| Rough Ground | 40 | grass, gravel, wooden bridges, rail crossings, stronger with speed |
| Engine | 0 | a hum rising with revs and throttle |
| Tire Blowouts | 80 | spike strips: a bang, then the flat tire thumping |
| Blown Engine | 80 | drag races and the sabotaged opening race |

**A level means the same strength on every row**: it is how hard both motors run in an ordinary instance of the effect
(a solid hit, a normal drift); bigger ones stay at that level. Changing a level plays a sample at that level. Rumble stops
when the game is paused, when Vibration is Off, when the window is not in front, and when the controller was not touched
for 30 seconds (so it does not buzz on the desk while you play on the keyboard).

## Settings, files

In the folder `scripts/nfsmw/` (next to the `.asi` files): `nfsmw_controller_support.ini` and `nfsmw_controller_support.log`. All optional; the Options > Rumble page and the Controls menu
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

Recommended with the widescreen fix (`NFSMostWanted.WidescreenFix.ini`): `ImproveGamepadSupport = 1` (the colourful
button icons, switching with the last input used), `LeftStickDeadzone = 0` and `RightStickDeadzone = 0` (this plugin
does it), `[CAMERA] Enable = 0`. NFS-XtendedInput overlaps with this plugin and also changes the Controls menu: do not
use both.

## Compatibility

Needs only the ASI loader and `speed.exe` 1.3 (and nfsmw_custom_menus for the Rumble page). Works with the widescreen fix
(recommended settings above) except that its own camera look (`[CAMERA] Enable = 1`) must be off: nfsmw_controller_support notices a
second redirect of the same call and switches its camera look off. Do not combine with NFS-XtendedInput. Every hook and row patch checks the game's code
first and is skipped if another mod changed it (the log says which). See the
[overview](../ReadMe.md#compatibility).

## Code layout

| File | What |
|---|---|
| `main.cpp` | the plugin entry: loads the settings, installs each part's hooks |
| `settings.cpp`, `pad.h` | `nfsmw_controller_support.ini`, the log, shared helpers |
| `input.cpp` | DirectInput state (dead zones, triggers, mouse deltas), analog action mapping |
| `bindings.cpp` | the PC binding table: the four look entries, a log of every binding change |
| `controls_menu.cpp` | the Controls menu: look rows, direction names, title icon |
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

## Limitations

- **XInput controllers only** (Xbox 360 / One / Series, and anything that looks like one: PlayStation and Switch pads
  through DS4Windows, Steam Input, ...). The plugin touches no other DirectInput device.
- **One player.** Only the first XInput controller and the game's port 0 are handled: no split-screen.
- **Rumble levels were tuned on one controller** (Xbox Wireless over Bluetooth); other pads may want different numbers.
- **The look bindings are not in the game's profile**: the profile saves only the game's own 19 controls, so the four look
  rows are kept in `nfsmw_controller_support.ini` (shared by all profiles).

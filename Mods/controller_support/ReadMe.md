# Controller Support

*Disclaimer: The code was written by Claude (Sonnet & Opus). The custom menu icon, testing, design, and this page, were made by me, a human.*

This mod for Need for Speed: Most Wanted 2005 (1.3) gives full controller support including analogue steering and throttle, camera look while driving, input remapping, customizable rumble, and complete menu navigation.

## What it does

### Car Controls

- **Analog steering, gas and brake**: The vanilla game maps them as on/off inputs, so even a stick bound to them was not analog. This mod enables analog throttle and reversing.
- Better default controller bindings. They can also be fully remapped in the game's controls menu, which will show accurate and precise button names.
- **Independent Triggers**: In the vanilla game, both triggers share one axis, so gas and brake on the triggers cancel each other. With this mod, you can hold gas and brake at the same time, just like you can on keyboard, causing a burnout.
- **Dead zones**: The sticks have a dead zone to address stick drift (15% by default), which can be changed in the ini (`left_stick_deadzone`, `right_stick_deadzone`). Changing it from a menu is a TODO.

### Camera look
- All 3rd-person car cameras can be controlled with either the mouse, or the right stick
- The latter is remappable in the Controls menu, if [Custom Menus](../custom_menus/ReadMe.md) is installed.
- After letting go of the "Look" inputs for a few seconds, the camera returns to its default position.
- This also works in the Look Behind view.

### Several controllers
- With more than one controller connected, each controller's keys can be remapped in the Controls menu.
- In single-player, all input devices (keyboard and all controllers) will be accepted.
- This fully integrates with my [Split Screen Support](../split_screen_support/ReadMe.md) mod, so everything will work like you would expect from a video game that has split screen: Both players can pause the game, control the menus, control their own cameras, have their own input mapping, have their own rumble, etc.

### Rumble
- Hook up various events to controller rumble, such as gear shifts, collisions, driving on grass, nitrous, and more.
- The rumble strength of each type of event can be cutomized per controller in **Options > Controls > a controller > Rumble Options**, if [Custom Menus](../custom_menus/ReadMe.md) is installed.

### Menu Navigation
- Menu navigation tested in all menus I could think of, including obscure menus like Showroom, Blacklist bio screen, Advanced Video Settings.
- Automatically switch to controller button prompts as soon as controller input is detected, and vice versa.
- Button prompts show Xbox buttons by default. If [Custom Menus](../custom_menus/ReadMe.md) is installed, you can switch to Playstation button prompts via **Options > Player > Controller Prompts**. Otherwise, you can set `button_prompts=playstation` in the ini.
- The world map can be panned with the right stick, and zoomed with the shoulder buttons/triggers.

### Translations
- The texts this mod adds to the UI (via the [Custom Menus](../custom_menus/ReadMe.md) mod), such as the Rumble Options, the camera look input mappings, and the controller button names, are translated into the game's nine other PC languages.

## Controller Compatibility

- Xbox 1 Controller: Tested both raw and via SteamInput, both work.
- Xbox 360 Controller: Not tested, expected to work.
- PS5 DualSense Controller: Tested with Steam Input, works, including rumble.
- Other controllers: Not tested, expected to work with Steam Input. Just add `speed.exe` to Steam as a non-steam game.
- Gyro controls: Not tested, not sure what you would use it for, but Steam Input can do it for you without a doubt.

## Mod Compatibility

- See [General Requirements](../../ReadMe.md#general-requirements).
- Recommended: [Custom Menus](../custom_menus/ReadMe.md) mod if you want to customize rumble or camera look keybindings. Otherwise, you have to hand-edit `scripts/mets_mods/nfsmw_controller_support.ini`.
- Recommended WideScreenFix .ini config:
```
ImproveGamepadSupport = 0
LeftStickDeadzone = 0
RightStickDeadzone = 0

[CAMERA]
Enable = 0
```
- Incompatible: NFS-XtendedInput, potentially other similar mods that mess with controller functions.

## TODOs
- The "Player->Controller Prompts" menu entry would be better if it was customizable per input device, so that when we switch from one controller to another, it might switch symbols.
- Add options to tweak deadzones of triggers and sticks from the menu
- Test per-controller rumble customization with >1 controller.
- Add pictures and videos showcasing the mod to this ReadMe.

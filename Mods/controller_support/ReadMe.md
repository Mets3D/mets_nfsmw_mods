# Controller Support

This mod for Need for Speed: Most Wanted 2005 (1.3) gives full controller support including analogue steering and throttle, camera look while driving, input remapping, customizable rumble, and complete menu navigation.

## What it does

### Car Controls

- **Analog steering, gas and brake**: The vanilla game maps them as on/off inputs, so even a stick bound to them was not analog. This mod enables analog throttle and reversing.
- Better default controller bindings. They can also be fully remapped in the game's controls menu, which will show accurate and precise button names.
- **Independent Triggers**: In the vanilla game, both triggers share one axis, so gas and brake on the triggers cancel each other. With this mod, you can hold gas and brake at the same time, just like you can on keyboard, causing a burnout.

### Camera look
- All 3rd-person car cameras can be controlled with either the mouse, or the right stick
- The latter is remappable in the Controls menu, if [Custom Menus](../custom_menus/ReadMe.md) is installed.
- After letting go of the "Look" inputs for a few seconds, the camera returns to its default position. With [Custom Menus](../custom_menus/ReadMe.md) installed, **Options > Player > Free Look Reset** sets how many seconds that takes for each player, or Off to leave the camera where you put it.
- Free look also works in the Look Behind view.

### Customizable Rumble
- Various events in-game will create controller rumble, such as gear shifts, collisions, driving on grass, nitrous, and more.
- The rumble strength of each type of event can be cutomized per controller in **Options > Controls > a controller > Settings**, if [Custom Menus](../custom_menus/ReadMe.md) is installed.

### Customizable Dead-zones
- You can change the deadzone of each stick, and the triggers, in the same menu as the Rumble settings mentioned above.
- The green bar under each dead zone slider shows the current activation value of the given stick/trigger. I recommend calibrating the deadzone by gently nudging the stick in all directions, letting go, and taking note of the highest value that the green bar can reach while not being touched.

### Several controllers
- With more than one controller connected, each controller can be customized separately. Each has their own dead zone, rumble, and button mapping stored separately.
- In single-player, all input devices (keyboard and all controllers) can control the game at the same time.
- Controller settings are stored per controller identity, as long as it has one. This ID is defined by the OS, and it can be defined by connecting USB port instead of actual device hardware ID. Steam Input's virtual controller has no ID. In such cases, its data is stored per controller slot, 1 to 4.
- Controllers can be connected while the game is running, no need to restart like in vanilla.
- This mod fully integrates with my [Split Screen Support](../split_screen_support/ReadMe.md) mod, so everything will work like you would expect from a video game that has split screen: Both players can pause the game, control the menus, control their own cameras, have their own controller customization as described above.

### Menu Navigation
- Menu navigation tested in all menus I could think of, including obscure menus like Showroom, Blacklist bio screen, Advanced Video Settings.
- Automatically switch to controller button prompts as soon as controller input is detected, and vice versa.
- Button prompts show Xbox buttons by default. Each controller has its own prompt style: if [Custom Menus](../custom_menus/ReadMe.md) is installed, you can switch a controller to PlayStation or Nintendo Switch button prompts via **Options > Controls > the controller > Settings > Controller Prompts**, and the prompts follow whichever controller you touched last.
- The world map can be panned with the right stick, and zoomed with the shoulder buttons/triggers.
- In the showroom, the triggers zoom the car view by how far they are pulled, and the shoulder buttons zoom like a trigger pulled fully.

### Translations
- The texts this mod adds to the UI (via the [Custom Menus](../custom_menus/ReadMe.md) mod), such as the Settings page, the camera look input mappings, and the controller button names, are translated into the game's nine other PC languages.

## Controller Compatibility

- Xbox 1 Controller: Tested both raw and via SteamInput, both work.
- Xbox 360 Controller: Not tested, expected to work.
- PS5 DualSense Controller: Tested with Steam Input, works, including rumble.
- Other controllers: Not tested, expected to work with Steam Input. Just add `speed.exe` to Steam as a non-steam game.
- Gyro controls: Not tested, not sure what you would use it for, but Steam Input can do it for you without a doubt.

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Recommended: [Custom Menus](../custom_menus/ReadMe.md) mod if you want to customize rumble, deadzones, button prompts, or camera look keybindings.
- Recommended WideScreenFix .ini config:
```
ImproveGamepadSupport = 0
LeftStickDeadzone = 0
RightStickDeadzone = 0

[CAMERA]
Enable = 0
```
- Incompatible: `NFS-XtendedInput`, potentially other similar mods that mess with controller functions.

## TODOs
- Add pictures and videos showcasing the mod to this ReadMe.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

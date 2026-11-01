# Controller Support

This mod for Need for Speed: Most Wanted 2005 (1.3) gives full controller support including analogue steering and throttle, camera look while driving, input remapping, customizable rumble, complete menu navigation, different button prompt styles (Xbox/PlayStation/Switch), customizable deadzones.

[YouTube link.](https://www.youtube.com/watch?v=BjmKBdCi4-0)

### Installation
Download the latest zip from the [Releases](../../../../releases) page and copy its `scripts` folder into the game's folder (next to `speed.exe`). If you don't want one of the mods, simply delete its `.asi` file.

For PS5 and Switch Controller, you must add `speed.exe` to Steam as a non-Steam game, and enable Steam Input.
<img width="463" height="224" alt="image" src="https://github.com/user-attachments/assets/263a9a77-71c3-48bb-83aa-b2b52654ceee" />

<img width="1313" height="599" alt="image" src="https://github.com/user-attachments/assets/d1b19956-55ba-4cf3-8ea8-d082ef21cfcb" />

## Car Controls

https://github.com/user-attachments/assets/3bad7b97-9660-49a8-9776-a41e7fd4e137

- **Analog steering, gas and brake**: The vanilla game maps them as on/off inputs, so even a stick bound to them was not analog. This mod enables analog throttle and reversing.
- Better default controller bindings. They can also be fully remapped in the game's controls menu, which will show accurate and precise button names.
- **Independent Triggers**: In the vanilla game, both triggers share one axis, so gas and brake on the triggers cancel each other. With this mod, you can hold gas and brake at the same time, just like you can on keyboard, causing a burnout.

## Better Defaults

https://github.com/user-attachments/assets/0e59ff57-ba4a-47a3-b170-997dbdaf55e1

The vanilla game's default keybindings don't use the analogue triggers for throttle and braking, and the Nitro button is the Start button, which is a bit ridiculous. This mod overhauls the game's default controller button mapping. Of course, they are still fully remappable. The modified defaults can be disabled in the mod's .ini config file.

## Camera look

https://github.com/user-attachments/assets/41dcddfd-1c1f-486e-9a8d-67fce9ff1864

- All 3rd-person car cameras can be controlled with either the mouse, or the right stick.
- The latter is remappable in the Controls menu, if [Custom Menus](../custom_menus/ReadMe.md) is installed.

<img width="600" alt="image" src="https://github.com/user-attachments/assets/a7c8b4ab-61a4-4936-8750-bb7ea30a8efa" />

- After letting go of the "Look" inputs for a few seconds, the camera returns to its default position. With [Custom Menus](../custom_menus/ReadMe.md) installed, **Options > Player > Free Look Reset** sets how many seconds that takes for each player, or Off to leave the camera where you put it.
- Free look also works in the Look Behind view.

## Customizable Dead-zones

<img width="600" src="https://github.com/user-attachments/assets/5a96565b-abf2-4810-a822-6e7c52403133" />

- You can change the deadzone of each stick, and the triggers, in the same menu as the Rumble settings mentioned above.
- The green bar under each dead zone slider shows the current activation value of the given stick/trigger. I recommend calibrating the deadzone by gently nudging the stick in all directions, letting go, and taking note of the highest value that the green bar can reach while not being touched.

## Customizable Rumble
<img width="600" src="https://github.com/user-attachments/assets/d215724f-e395-4114-a029-6c5a161a881a" />

Note that any controller with a low battery will fail to rumble due to the physics of motors and batteries. If you're not getting rumble, always check battery first.
- Various events in-game will create controller rumble, such as gear shifts, collisions, driving on grass, nitrous, and more.
- The rumble strength of each type of event can be cutomized per controller in **Options > Controls > a controller > Settings**, if [Custom Menus](../custom_menus/ReadMe.md) is installed.

## Several controllers

- With more than one controller connected, each controller can be customized. Their dead zones, rumble, and button mappings are stored per savefile.
- In single-player, all input devices (keyboard and all controllers) can control the game at the same time.
- Controller settings are stored per device ID.
- Controllers can be connected while the game is running, no need to restart the game.
- This mod fully integrates with my [Split Screen Support](../split_screen_support/ReadMe.md) mod, so everything will work like you would expect from a video game that has split screen: Both players can pause the game, control the menus, control their own cameras, have their own controller customization as described above.

## Menu Navigation

https://github.com/user-attachments/assets/f0deea86-22e9-4668-bf13-1ae567141eed

- Menu navigation tested in all menus I could think of, including obscure menus like Showroom, Blacklist bio screen, Advanced Video Settings, and the world map.
- Automatically switch to the button prompts of the last active input device, including keyboard/mouse.
- Automatically assign correct button prompt style for Playstation and Nintendo controllers when the controller is first detected.
- If [Custom Menus](../custom_menus/ReadMe.md) is installed, you can switch a controller to PlayStation or Nintendo Switch button prompts via **Options > Controls > the controller > Settings > Controller Prompts**.

## Controller Compatibility
- XInput controllers (xbox controllers and many 3rd-party controllers) are supported with or without SteamInput.
- Other controllers like PS5 and Switch are supported via Steam Input. To use Steam Input, add `speed.exe` to your Steam Library as a non-Steam game, and then **right click->Properties->Controller->Set "Enable Steam Input" instead of "Use default Settings"**. This is not Steam's default as of 2026 October, you have to do this yourself.
- PS5 controller was tested fully working, including rumble, deadzone customization, and automatically using PS5 button prompts.
- Nintendo Switch Pro controller also tested and fully working. Steam Input will by default map Nintendo Switch buttons in a way that supports the muscle memory of Nintendo gamers. If your Switch controller is to be used by a gamer who grew up with Xbox or Playstation, you should do two things: 1) In Steam Input, swap the mapping of X/Y and A/B. 2) In the game, set the controller's Prompt Style from "Switch (Nintendo Layout)" to just "Switch". Then your Switch controller will have the same button positions as an Xbox or PS controller.
- Gyro controls: Tested with Steam Input, it worked with the Switch Pro but not with the DualSense 5. Makes menu navigation annoying, since there's no way to differentiate gyro inputs from regular ones. Unless I make it so that the menu cna only be navigated with the d-pad, but it would be a hassle and I think the gyro is gimmicky so I didn't bother.

## Mod Compatibility
- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Recommended: [Custom Menus](../custom_menus/ReadMe.md) mod if you want to customize rumble, deadzones, button prompts, or camera look keybindings.
- WideScreenFix: `ImproveGamepadSupport = 0` will be forced by this mod for compatibility.
- Incompatible: `NFS-XtendedInput`, potentially other similar mods that mess with controller functions.

### Translations
- The texts this mod adds to the UI (via the [Custom Menus](../custom_menus/ReadMe.md) mod), such as the Settings page, the camera look input mappings, and the controller button names, are translated into the game's nine other PC languages.

### AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, were hand-made.

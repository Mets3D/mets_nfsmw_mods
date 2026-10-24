# Custom Menus

This mod for Need for Speed: Most Wanted 2005 (1.3) provides other mods with the ability to add their own controls and menus in the game's main menu and pause menu easily, without conflicts, and in a way that feels like a native part of the game.

## Example Mods
- [Camera Settings](../camera_settings/ReadMe.md): Adds the Options > Camera page.
- [Controller Support](../controller_support/ReadMe.md): Adds the Settings page with dead zones and rumble (opened from the Controls screen), and the Look and Previous Camera View rows on the Controls screen.

## Features
- Mods can add their own Options pages, which can be visible in the pause menu and main menu's Options menus.
  - An options menu consists of rows, which contain a label and a value.
  - The value can be displayed as text or numbers, or a slider.
  - Rows can be grayed out based on other values.
  - Each row can have a Help text to explain what it does, shown when the player presses the Help button.
- The button layout mimics other option menus in the game:
  - **Accept**: Applies your changes and leaves the menu.
  - **Back**: A pop-up confirms if you want to discard your changes.
  - **Defaults**: A pop-up asks for confirmation. Default values are provided by the mod that is creating the menu.
  - **Help**: Shows a description of the currently selected value. The help text is provided by the mod that is creating the menu.
- **Localization**: Mods can provide translations for the game's 9 supported additional languages for all labels, values, Help texts, keybindings that they're adding.
- **Add button mappings**: Mods can add new key and button bindings to Options > Controls, and rename the game's texts.
- **Expand existing menus**: Mods can also add options to the end of the game's menu screens.
- **Sub-menus**: Mods can also define options menus that are accessed in some other way rather than through the regular Options menu list. For example, the Controller Settings page of the Controller Support mod.

## Other fixes & tweaks

- **Controls screen title icon.** Pause > Options > Controls showed the audio icon as its title icon (a base game bug). It now shows the Controls icon.
- **Skippable cutscenes.** All pre-rendered cutscenes can be skipped with Enter / A.
- **Fewer pop-ups.** Many superfluous menu pop-ups are removed.
- **Simpler leaving questions.** "Are you sure you want to exit without accepting changes? All changes will be lost." (where Yes lost your changes) and the Controls menu's "Do you wish to keep the modified settings?" both ask "Save changes?" now: Yes keeps the changes and leaves, No loses them and leaves, Back/B stays in the menu.
- **Cycling strip menus.** Strip menus like the pause menu now loop, eg. pressing left on the left-most icon jumps to the right-most icon, and vice versa.
- **Skip Career Menu.** You can go straight from the main menu into your career, without having an in-between menu which has the same functionality as the "Alias Manager".

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Incompatible:
  - `NFS-XtendedInput` due to touching `UIOptionsMain::Setup`.

## TODO
- Ability to add not just sub-menus under "Options", but also in the pause menu, eg. to customize the current race, or other dev tools.

## Using it from your own mod

Copy [`common/custom_menus_api.h`](../common/custom_menus_api.h) (it has no dependencies) and export `nfsmw_custom_menus_provide`. The framework calls it in every loaded mod the first time the Options menu opens, and a mod works without its page when the framework is missing.

[`example_options_page/src/main.cpp`](example_options_page/src/main.cpp) is a complete example mod with a page of dummy settings (a slider, a choice and an On/Off row that uses the game's own strings), and its header comment explains every callback, the rows, the icons and the other places rows can go. Copy from there; it also shows how a mod ships translations of its texts.

## Files and settings

In the folder `scripts/mets_mods/` (next to the `.asi` files): 
- [`nfsmw_custom_menus.ini`](nfsmw_custom_menus.ini) to configure the mod.
- `nfsmw_custom_menus.log`: Debug logging from the mod.

## AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, are hand-made.

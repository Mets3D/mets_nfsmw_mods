# Custom Menus

This mod for Need for Speed: Most Wanted 2005 (1.3) allows other mods to easily add their own controls and menus to the game's main menu and pause menu, without conflicts and in a way that feels like a native part of the game. This mod is now considered complete, but PRs are welcome.

## Example Mods
- [Camera Settings](../camera_settings/ReadMe.md): Adds the Options > Camera page.
- [Controller Support](../controller_support/ReadMe.md): Adds the Settings page with deadzone and rumble settings (opened from the Controls screen), and the Look and Previous Camera View rows on the Controls screen.

## Features
- Mods can add their own pages to the Pause Menu, Main Menu, and Options Menu.
  - An options menu consists of rows, which contain a label and a value.
  - The value can be displayed as text, a number, or a slider.
  - Rows can be grayed out based on other values.
  - Each row can have help text to explain what it does, shown when the player presses the Help button.
- The button layout mimics other options menus in the game:
  - **Accept**: Applies your changes and leaves the menu.
  - **Back**: A pop-up asks whether you want to discard your changes.
  - **Defaults**: A pop-up asks for confirmation. Default values are provided by the mod that is creating the menu.
  - **Help**: Shows a description of the currently selected value. The help text is provided by the mod that is creating the menu.
- **Localization**: Mods can provide translations in the game's nine additional supported languages for all labels, values, help text, and keybindings they add.
- **Add button mappings**: Mods can add new key and button bindings to Options > Controls and rename the game's text.
- **Expand existing menus**: Mods can also add options to the end of the game's menu screens and to the Quick Race options (any race type; the list scrolls when the screen's 9 rows are not enough).
- **Sub-menus**: Mods can also define options menus that are accessed in some other way rather than through the regular Options menu list. One example is the Controller Settings page of the Controller Support mod.

## Other fixes & tweaks

- **Fewer pop-ups.** Many superfluous menu pop-ups are removed.
- **Skippable cutscenes.** All pre-rendered cutscenes can be skipped with Enter / A.
- **Simpler exit prompts.** Menus asking if you want to save were inconsistent where some would ask "Want to save?" and others would save "Want to discard?". The mod makes both cases ask "Save changes?": Yes saves the changes and leaves the menu, No discards them and leaves the menu, and Back/B stays in the menu.
- **Cycling strip menus.** Strip menus like the pause menu now loop; for example, pressing left on the leftmost icon jumps to the rightmost icon, and vice versa.
- **Skip Career Menu.** You can go straight from the main menu into your career without going through an intermediate menu that has the same functionality as the "Alias Manager".
- **Fix leaving nested menus:** Going out pf a nested strip menu puts you on the entrance menu instead of the left-most menu item.
- **Fix Controls screen icon:** The vanilla game's Pause > Options > Controls menu was incorrectly using the Audio Options icon.
- **Fix Quick Race options:** In menus like the Quick Race options, values would turn dark after stepping over them in the vanilla game.

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Incompatible:
  - `NFS-XtendedInput` due to touching `UIOptionsMain::Setup`.

## Using it from your own mod

Copy [`common/custom_menus_api.h`](../common/custom_menus_api.h) (it has no dependencies) and export `nfsmw_custom_menus_provide`. The framework calls it in every loaded mod the first time the Options menu opens, and a mod works without its page when the framework is missing.

[`example_options_page/src/main.cpp`](example_options_page/src/main.cpp) is a complete example mod with a page of dummy settings (a slider, a choice, and an On/Off row that uses the game's own strings), and its header comment explains every callback, the rows, the icons, and the other places rows can go. Copy from there; it also shows how a mod ships translations of its text.

## Files and settings

In the folder `scripts/mets_mods/` (next to the `.asi` files):
- [`nfsmw_custom_menus.ini`](nfsmw_custom_menus.ini) to configure the mod.
- `nfsmw_custom_menus.log`: Debug logging from the mod.

## AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page were done by hand.

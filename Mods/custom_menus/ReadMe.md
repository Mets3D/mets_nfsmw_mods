# Custom Menus

This mod for Need for Speed: Most Wanted 2005 (1.3) provides other mods with the ability to add their own controls and menus in the game's main menu and pause menu easily, without conflicts, and in a way that feels like a native part of the game.

## Example Mods
- [Camera Settings](../camera_settings/ReadMe.md): Adds the Options > Camera page.
- [Controller Support](../controller_support/ReadMe.md): Adds the Rumble page (opened from the Controls screen), and the Look and Previous Camera View rows on the Controls screen.

## Features
- Mods can add their own Options pages: an entry with the page's icon after "Controls" in the pause menu's Options (and, if the page asks for it, the main menu's Options, before EA Trax and Credits).
- For each row, the mod must provide:
  - A label (its own text, or one of the game's strings)
  - A help text (shown when player presses Help button).
  - Its kind: a value row, a slider bar row (like Audio's volumes), or the game's own Camera selector. Kinds can be mixed in one page.
- For the page, the mod decides what happens when:
  - A value is shown (any text: degrees, percent, On/Off...)
  - Left / Right is pressed (the mod picks the step; Shift or LB asks for a smaller one)
  - The page is accepted, discarded or reset to defaults
- **Accept / Back** work like the game's own: Leaving the menu with "Accept" keeps changes; "Back" asks "discard changes?" if something changed. **Defaults** asks for confirmation first.
- **Rows in the game's own Player options**: a page can also put its rows at the end of Options > Player (`placement = kPlayerOptions`).
- **Pages opened by a mod**: a page with `placement = kHidden` has no entry in the Options menu; the mod opens it with `open_page` from another screen (for example the Rumble Options button in the Controller Support mod).
- **Rows on the Controls screen**: a mod can add rows for its own key and button bindings to Options > Controls, and rename the game's texts.

## Other fixes

- **Controls screen title icon.** Pause > Options > Controls showed the audio icon as its title icon (a base game bug). It now shows the Controls icon.
- **Fewer pop-ups.** Certain action confirmation dialogue boxes are removed.
- **Skippable cutscenes.** All pre-rendered cutscenes can be skipped with Enter / A.
- **Skip Career Menu.** You can go straight from the main menu into your career, without having an in-between menu which has the same functionality as the "Alias Manager".

## Mod Compatibility

- See [General Requirements](../../ReadMe.md#general-requirements).
- Incompatible:
  - NFS-XtendedInput due to touching `UIOptionsMain::Setup`.
  - NFS MW Redux v3 mod pack.

## TODO
- Test translation support?
- Ability to add not just sub-menus under "Options", but also in the pause menu, eg. to customize the current race, or other dev tools.
- We now have the ability to add entries in the Player Options, but what about to the other menus? Should probably support all of them.

## Using it from your own plugin

Copy [`common/custom_menus_api.h`](../common/custom_menus_api.h) (it has no dependencies) and export `nfsmw_custom_menus_provide`. The framework calls it in every loaded plugin the first time the Options menu opens, and a plugin works without its page when the framework is missing.

[`example_options_page/main.cpp`](../example_options_page/main.cpp) is a complete example plugin with a page of dummy settings (a slider, a choice and an On/Off row that uses the game's own strings), and its header comment explains every callback, the rows, the icons and the other places rows can go. Copy from there.

## Files and settings

In the folder `scripts/mets_mods/` (next to the `.asi` files): 
- [`nfsmw_custom_menus.ini`](nfsmw_custom_menus.ini) to configure the mod.
- `nfsmw_custom_menus.log`: Debug logging from the mod.

## AI Disclaimer

The code was written by Claude (Sonnet & Opus). Testing, design, and this page, are hand-made.

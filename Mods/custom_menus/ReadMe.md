# Custom Menus

*Disclaimer: The code was written by Claude (Sonnet & Opus). The custom menu icon, testing, design, and this page, were made by me, a human.*

This mod for Need for Speed: Most Wanted 2005 (1.3) provides other mods with an ability to add their own menus in the game's main menu and pause menu easily, without conflicts, and in a way that feels like a native part of the game.

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
- **Fewer pop-ups.** Certain action confirmation dialogue boxes are removed. To bring them back, add to the .ini:
```
[features]
skip_dialogs=0
```
- **Skip Redundant Career Menu.** You can go straight from the main menu into your career, without having an in-between menu which has the same functionality as the "Alias Manager". To disable this, add to the .ini:
```
[features]
resume_career=0
```

## Mod Compatibility

- Required: Game version 1.3
- Required: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- Incompatible:
  - NFS-XtendedInput due to touching `UIOptionsMain::Setup`.
- Confirmed compatible:
  - "DRIFT" and "Pursuit" camera modes added by [`NFSMWExtraOptions`](https://github.com/ExOptsTeam/NFSMWExOpts).

## TODO
- Translation support?
- Ability to add not just sub-menus under "Options", but also in the pause menu, eg. to customize the current race, or other dev tools.
- We now have the ability to add entries in the Player Options, but what about to the other menus? Should probably support all of them.

<details>

<summary>Developer Notes</summary>

## Using it from your own plugin

Copy [`common/custom_menus_api.h`](../common/custom_menus_api.h) (it has no dependencies) and export one function:

```cpp
#include "custom_menus_api.h"
namespace opt = nfsmw::custom_menus;

static int g_strength = 50, g_opened_strength;  // your own state

static const opt::Row kRows[] = {
    // kind, label, label_hash (0 = use the label), help text
    // opt::kSliderRow instead draws the game's slider bar, filled to `fill(row)` (0..1); `value` is then only the fallback
    {opt::kValueRow, "Strength", 0, "How strong the effect is. Hold Shift or LB for finer steps."},
};

static unsigned value(int row, char *out, unsigned size) {
    snprintf(out, size, "%d%%", g_strength);
    return 0;  // or return a language hash of the game, and `out` is ignored
}
static void change(int row, int direction, bool fine) {
    g_strength += direction * (fine ? 1 : 5);
    if (g_strength < 0) g_strength = 0;
    if (g_strength > 100) g_strength = 100;
}
static void opened() { g_opened_strength = g_strength; }
static bool changed() { return g_strength != g_opened_strength; }
static void accept() { /* save to your ini */ g_opened_strength = g_strength; }
static void discard() { g_strength = g_opened_strength; }
static void defaults() { g_strength = 50; }

static opt::Page g_page;

extern "C" __declspec(dllexport) void nfsmw_custom_menus_provide(const opt::Host *host) {
    g_page = {"mymod", "My Mod", "MY MOD", "MY MOD INFO",
              /* icon hash */ 0, /* fallback icon hash, 0 = the Options icon */ 0, /* .tpk with the icon, or null */ nullptr,
              1, kRows,
              /* available */ nullptr, opened, value, change, changed, accept, discard, defaults};
    host->register_page(&g_page);
}
```

The framework calls `nfsmw_custom_menus_provide` in every loaded plugin **the first time the Options menu opens**, so plugin
load order does not matter. If `nfsmw_custom_menus.asi` is missing, your plugin simply never gets asked: it should work
without its page.

Rules for the callbacks (all called on the game's thread):

| Callback | Called | Notes |
|---|---|---|
| `available` | each time the Options menu opens (the main menu's too, for `kNewEntryEverywhere`) | `nullptr` = always. Return false to hide the entry (no race loaded, no controller, ...). |
| `opened` | the page opens | Remember what is current: it is what Back restores. |
| `value(row, out, size)` | whenever a row is drawn | Write text to `out` (63 characters at most; `\x01` is the degree sign) and return 0, or return a **language hash** of the game and leave `out` alone. **Required.** |
| `fill(row)` | whenever a `kSliderRow` is drawn | Optional (`Page::fill`, after `placement`): the bar's fill, 0 to 1. Without it the row shows `value` as text. |
| `change(row, direction, fine)` | left (-1) or right (+1) on a row | Take effect immediately. Hold Shift or LB = `fine`. **Required.** |
| `changed` | Back is pressed, and when the screen is left | Do the values differ from when the page opened? |
| `accept` | the screen is left and `changed()` is true | Keep and save the values. After Back and confirm, `discard` ran first, so `changed()` is false and `accept` is not called. |
| `discard` | Back and "discard" confirmed | Put back what `opened` saw. |
| `defaults` | Defaults confirmed | Reset the page (the selected view, for a camera page). |

Row labels: either text (`label`, title case; the framework gives it a language hash) or the hash of one of the game's
own strings (`label_hash`), for example `0xDDDC5E1B` for "Vibration". `help` is plain text. Icons: `icon` is the hash of
a texture (a texture's hash is the game's `h = h * 33 + c` from `0xFFFFFFFF` over the upper-case name; `texture_hash()`
in [`common/nfsmw_util.h`](../common/nfsmw_util.h)); if the game has no such texture, `fallback_icon` (any texture the game
always has) is used, or with 0 the game's Options icon (two cogs). To ship your own icon, build a `.tpk` with
[`Tools/tpk_build.py`](../../Tools/tpk_build.py) from a 256x256 PNG named like the texture (`OPTIONS_MYMOD.png`), put it in
`scripts/mets_mods/` (`nfsmw::data_file()` in `common/nfsmw_util.h` gives the path), and pass its full path as `texture_pack`; the framework loads it through the game's resource loader when
the menu first opens.

The `Page` and `Row` structures and the arrays they point to must stay valid for the whole run (make them static).

## Limitations

- Rows can't be added or removed while a page is open.

## Files and settings

In the folder `scripts/mets_mods/` (next to the `.asi` files): `nfsmw_custom_menus.log` (what registered, hooks), `nfsmw_custom_menus_crash.log` (any access violation in the
game process, with the modules involved: useful for every mod, not only this one), `nfsmw_custom_menus.ini`:

```ini
[features]
help_hint=0          ; no "2 Help" hint (the 2 key / X button still work)
hide_dialog_icon=0   ; keep the exclamation mark in the Help message box
texture_pack=0       ; do not load the pages' icon packs
skip_dialogs=0       ; ask the confirmations again
resume_career=0      ; the main menu's Career entry opens the Career main menu again
[hooks]
notify=0             ; skip one hook, for troubleshooting (names: optmain initialpos addoption setupplayer setupvideo queue_switch
                     ; notify restoredefaults didnotchange restoreoriginals dialogbuild controlsicon showdialog memcardmessage
                     ; saveconfirm updateprofile careercrib maincareer; the Controls rows have their own, see the log)
```

## How it works (short)

A page's screen is the game's `UIOptionsScreen` in its **Player** category, with the Player rows replaced by the page's
(`SetupPlayer` is hooked to do that). The entry in the Options menu is an `IconOption` added right before
`IconScroller::SetInitialPos` lays the icons out (in the main menu, right before the game adds EA Trax). The Help hint is a clone of the layout's Defaults hint
(`FEGroup::Clone` plus fixes for what the game leaves broken in a clone), and the Help key is polled on the screen's
per-frame tick, because the layout does not forward that key as a message. Offsets come from the PC disassembly (the decomp's
are from console builds); findings and addresses are in [`docs/menus-and-camera.md`](../../docs/menus-and-camera.md).

</details>
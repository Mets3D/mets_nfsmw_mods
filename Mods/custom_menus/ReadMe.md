# Custom Menus

*Disclaimer: The code was written by Claude (Sonnet & Opus). The custom menu icon, testing, design, and this page, were made by me, a human.*

This mod for Need for Speed: Most Wanted 2005 (1.3) provides other mods with an ability to add their own menus in the game's pause menu easily, without conflicts, and in a way that feels like a native part of the game.

## What the framework does for a page

- Adds an entry with the page's icon to the Options menu (pause menu only), after the game's own entries.
- Builds the screen from the game's own Options layout, so fonts, sounds, animations, mouse hover and the controller
  all work as they do in the vanilla game's screens.
- Left / right on a row tells the mod that owns the page (through its `change` callback) to raise or lower that value.
  The mod decides what the stepping value of that row is. Holding **Shift** tells it to take a smaller step.
- **Accept / Back** work like the game's own: Leaving the menu with "Accept" keeps changes; "Back" asks "discard changes?" if something changed
- **Defaults**: Asks for confirmation, then resets the page.
- **Help**: a "2 Help" hint next to the game's "1 Defaults" hint (a copy of it, so it matches). The **2** key, a click on
  the hint, or the **X** button on an XInput controller opens a message box with the highlighted row's help text.
- Game strings: all texts a page gives are added to the game's language table, so the game's own label, title and
  dialog code shows them.
- **Rows in the game's own Player options**: a page can also put its rows at the end of Options > Player (`placement = kPlayerOptions`).
- Row kinds: a normal value row, and the game's own **Camera selector** (it switches the player's camera view; the other
  rows are redrawn, so a camera page can show the selected view's values).

## Other fixes

- **Controls screen title icon.** Pause > Options > Controls showed the audio icon as its title icon (a base game bug:
  its layout defaults to it and the screen never sets its own). It now shows the Controls icon. Works without any page
  or other mod (`title_icon.cpp`).

## Example Mods
- [Camera Settings](../camera_settings/ReadMe.md): Adds the Options > Camera page.
- [Controller Support](../controller_support/ReadMe.md): Adds the Options > Rumble page.

## TODO
- Translation support?
- The Advanced Video menu has a scrollbar - Can we have unlimited options, and the scrollbar automatically appear when needed?
- Ability to add not just sub-menus under "Options", but also in the pause menu.
- We now have the ability to add entries in the Player Options, but what about to the other menus? Should probably support all of them.
- Maybe investigate other menus later.
- In the main menu, we currently get a "Quit" button hint that shows a stick down. Is this a bug in the vanilla game, the widescreen fix, in this mod, or the controller support mod? It's not clear from the symbol which stick it means, and neither of them do anything anyways.
- Is there a limit on how many entries can be in a page?
- Adding entries to the controls? Can this be an unlimited amount, since the controls UI supports a scrollbar?

<details>

<summary>Code Notes</summary>

## Using it from your own plugin

Copy [`common/custom_menus_api.h`](../common/custom_menus_api.h) (it has no dependencies) and export one function:

```cpp
#include "custom_menus_api.h"
namespace opt = nfsmw::custom_menus;

static int g_strength = 50, g_opened_strength;  // your own state

static const opt::Row kRows[] = {
    // kind, label, label_hash (0 = use the label), help text
    {opt::kValueRow, "Strength", 0, "How strong the effect is. Hold Shift for finer steps."},
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
              /* icon hash */ 0, /* fallback icon hash */ 0x66C9A7B6, /* .tpk with the icon, or null */ nullptr,
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
| `available` | each time the pause menu's Options opens | `nullptr` = always. Return false to hide the entry (no race loaded, no controller, ...). |
| `opened` | the page opens | Remember what is current: it is what Back restores. |
| `value(row, out, size)` | whenever a row is drawn | Write text to `out` (63 characters at most; `\x01` is the degree sign) and return 0, or return a **language hash** of the game and leave `out` alone. **Required.** |
| `change(row, direction, fine)` | left (-1) or right (+1) on a row | Take effect immediately. Hold Shift = `fine`. **Required.** |
| `changed` | Back is pressed, and when the screen is left | Do the values differ from when the page opened? |
| `accept` | the screen is left and `changed()` is true | Keep and save the values. After Back and confirm, `discard` ran first, so `changed()` is false and `accept` is not called. |
| `discard` | Back and "discard" confirmed | Put back what `opened` saw. |
| `defaults` | Defaults confirmed | Reset the page (the selected view, for a camera page). |

Row labels: either text (`label`, title case; the framework gives it a language hash) or the hash of one of the game's
own strings (`label_hash`), for example `0xDDDC5E1B` for "Vibration". `help` is plain text. Icons: `icon` is the hash of
a texture (a texture's hash is the game's `h = h * 33 + c` from `0xFFFFFFFF` over the upper-case name; `texture_hash()`
in [`common/nfsmw_util.h`](../common/nfsmw_util.h)); if the game has no such texture, `fallback_icon` (any texture the game
always has, for example `0x66C9A7B6`, MILESTONE_SPEEDTRAP) is used. To ship your own icon, build a `.tpk` with
[`Tools/tpk_build.py`](../../Tools/tpk_build.py) from a 256x256 PNG named like the texture (`OPTIONS_MYMOD.png`), put it in
`scripts/mets_mods/` (`nfsmw::data_file()` in `common/nfsmw_util.h` gives the path), and pass its full path as `texture_pack`; the framework loads it through the game's resource loader when
the menu first opens.

The `Page` and `Row` structures and the arrays they point to must stay valid for the whole run (make them static).

## Limitations

- **Pause menu only.** Pages are added to Options when the game is paused in a race. The frontend (main menu) Options
  screen is not touched.
- **Fixed rows.** The row list is read once when the page registers: no rows added or removed while the screen is open.
- **Page and row counts.** At most 8 pages and 40 rows per page (constants in `custom_menus/main.cpp`). The Options menu's
  icon strip has a fixed number of slots in the layout; two extra entries were tested; more may not fit, and the game
  drops entries it has no slot for (the log says so).
- **Defaults is replaced while a page is open.** The game's own Player-category defaults are not applied then. (A page
  is built on the Player category's screen: that is why Accept / Back / Defaults come for free.)

Ideas for later: sliders, toggles and sub-pages as row kinds; the frontend Options menu; translations; rows that
change at run time; icons in the game's own style as a template.

## Compatibility

Needs only the ASI loader and `speed.exe` 1.3 (SHA-256 `0C5675A0...2067E342`; all addresses and offsets are for it). It conflicts with any mod that hooks the same Options screen functions or
changes the same code (NFS-XtendedInput does: it jumps over code inside `UIOptionsMain::Setup`); the hooks are skipped
when another mod changed their first bytes, and then there are no pages. See the [overview](../ReadMe.md#compatibility).

## Files and settings

In the folder `scripts/mets_mods/` (next to the `.asi` files): `nfsmw_custom_menus.log` (what registered, hooks), `nfsmw_custom_menus_crash.log` (any access violation in the
game process, with the modules involved: useful for every mod, not only this one), `nfsmw_custom_menus.ini`:

```ini
[features]
help_hint=0          ; no "2 Help" hint (the 2 key / X button still work)
hide_dialog_icon=0   ; keep the exclamation mark in the Help message box
texture_pack=0       ; do not load the pages' icon packs
[hooks]
notify=0             ; skip one hook (names: optmain initialpos setupplayer notify restoredefaults didnotchange
                     ; restoreoriginals dialogbuild controlsicon), for troubleshooting
```

## How it works (short)

A page's screen is the game's `UIOptionsScreen` in its **Player** category, with the Player rows replaced by the page's
(`SetupPlayer` is hooked to do that). The entry in the Options menu is an `IconOption` added right before
`IconScroller::SetInitialPos` lays the icons out. The Help hint is a clone of the layout's Defaults hint
(`FEGroup::Clone` plus fixes for what the game leaves broken in a clone), and the Help key is polled on the screen's
per-frame tick, because the layout does not forward that key as a message. Offsets come from the PC disassembly (the decomp's
are from console builds); findings and addresses are in [`notes.md`](../../notes.md).

</details>
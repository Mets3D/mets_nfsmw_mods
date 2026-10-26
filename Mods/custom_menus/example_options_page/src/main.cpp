/*
 * nfsmw_example_options_page: an example for mod makers, not for players. It adds a page of dummy settings to the Options menu
 * with nfsmw_custom_menus (the Options menu framework), and is the place to copy from when a mod wants a page of its own. It is
 * built with the others, and put in a game with `set_mods.py --mode ours --only custom_menus example_options_page` (or copy
 * nfsmw_example_options_page.asi into <game>/scripts/ next to nfsmw_custom_menus.asi) to see it (Pause > Options > Example Page, and the
 * main menu's Options).
 *
 * How it works:
 *  - Copy common/custom_menus_api.h (it has no dependencies) and export `nfsmw_custom_menus_provide`.
 *  - The framework calls that export in every loaded plugin the first time the Options menu opens, so plugin load order does not
 *    matter. If nfsmw_custom_menus.asi is missing, the plugin is simply never asked: it should work without its page.
 *  - All callbacks are called on the game's thread. The Page and Row structures and the arrays they point to must stay valid for
 *    the whole run (make them static).
 *
 * The callbacks (see Page in custom_menus_api.h for the full list):
 *
 *   available   each time the Options menu opens (the main menu's too, for kNewEntryEverywhere). nullptr = always. Return false to
 *               hide the entry (no race loaded, no controller, ...).
 *   opened      the page opens. Remember what is current: it is what Back restores.
 *   value       whenever a row is drawn. Write text to `out` (63 characters at most; '\x01' is the degree sign) and return 0, or
 *               return a language hash of the game and leave `out` alone. Required.
 *   fill        whenever a kSliderRow is drawn. Optional (Page::fill, after `placement`): the bar's fill, 0 to 1. Without it the
 *               row shows `value` as text.
 *   change      left (-1) or right (+1) on a row. Take effect immediately. Hold Shift or LB = `fine`. Required.
 *   changed     Back is pressed, and when the screen is left: do the values differ from when the page opened?
 *   accept      the screen is left and changed() is true: keep and save the values. After Back and confirm, `discard` ran first,
 *               so changed() is false and accept is not called.
 *   discard     Back and "discard" confirmed: put back what `opened` saw.
 *   defaults    Defaults confirmed: reset the page (the selected view, for a camera page).
 *
 * Rows: the label is either text (`label`, title case; the framework gives it a language hash) or the hash of one of the game's own
 * strings (`label_hash`, see the third row). `help` is plain text, shown by the 2 key / X button.
 *
 * Icons: `icon` is the hash of a texture (the game's h = h * 33 + c from 0xFFFFFFFF over the upper-case name; texture_hash() in
 * common/nfsmw_util.h); if the game has no such texture, `fallback_icon` (any texture the game always has) is used, or with 0 the
 * game's Options icon (two cogs, used here). To ship your own icon, build a .tpk with Tools/tpk_build.py from a 256x256 PNG named
 * like the texture (OPTIONS_MYMOD.png), put it in scripts/mets_mods/ (nfsmw::data_file() gives the path) and pass its full path
 * as `texture_pack`: the framework loads it through the game's resource loader when the menu first opens.
 *
 * Translations: `host->register_translations(folder)`, called first thing in the provide export, with a folder of your own (anywhere:
 * a subfolder of your mod's data folder, say) that holds one UTF-8 file per language, named like the game's language (`Language=` in
 * its Settings.ini): German.txt, French.txt, Spanish.txt, Italian.txt, Dutch.txt, Swedish.txt, Danish.txt, Polish.txt, Finnish.txt.
 * A line is `English text = Translation`, # starts a comment. Everything the mod gives the framework (page and row texts, help,
 * register_text) is looked up there by its English text, and so are the texts value() writes; a text without a translation stays
 * English. See ../translations/German.txt (next to src/) (here installed to scripts/mets_mods/translations/example_options_page/).
 *
 * Other places for rows (Page::placement): the end of the game's own Options > Audio, Video, Advanced Video, Gameplay or Player
 * (kAudioOptions, kVideoOptions, kAdvancedVideoOptions, kGameplayOptions, kPlayerOptions), a page that only the mod opens (kHidden), an
 * entry in the main menu's Options as well (kNewEntryEverywhere). The Controls screen takes binding rows (Host::register_controls_rows).
 *
 * Entries in the pause menu and the main menu themselves: kPauseMenuEntry, kMainMenuEntry and kPauseAndMainMenuEntry put an icon before the
 * menu's own Options entry, which opens the page (for things that are not settings, say tools for the current race). Three extra pages here
 * with the same rows test them: "Pause Menu Test" must only be in the pause menu, "Main Menu Test" only in the main menu, "Both Menus Test"
 * in both. Pressing one opens the page, and Back / Accept come back to the menu. `available` is asked each time the menu is built; the pause
 * menu's page is hidden by `[example] hide_pause_entry=1` in scripts/mets_mods/nfsmw_example_options_page.ini, to test that.
 */

#include <nfsmw_sdk/nfsmw_sdk.h>

#include <cstdio>

#include "custom_menus_api.h"
#include "nfsmw_util.h"

NFSMW_PLUGIN_DECLARE("NFSMW Example Options Page", "1.0.0", "Demeter Dzadik")

namespace opt = nfsmw::custom_menus;

namespace {

// ---- the dummy settings (your own state) ----
constexpr const char *kModes[] = {"Low", "Medium", "High"};

struct Settings {
    int strength = 50;  // 0..100
    int mode = 1;       // index into kModes
    bool enabled = true;
};
Settings g_now, g_opened;  // the live values, and what they were when the page opened
char g_ini[MAX_PATH];

void load() {
    nfsmw::data_file("nfsmw_example_options_page.ini", g_ini, sizeof(g_ini));
    g_now.strength = GetPrivateProfileIntA("example", "strength", g_now.strength, g_ini);
    g_now.mode = GetPrivateProfileIntA("example", "mode", g_now.mode, g_ini);
    g_now.enabled = GetPrivateProfileIntA("example", "enabled", g_now.enabled, g_ini) != 0;
    if (g_now.strength < 0 || g_now.strength > 100) g_now.strength = 50;
    if (g_now.mode < 0 || g_now.mode > 2) g_now.mode = 1;
}

void save() {
    char text[16];
    snprintf(text, sizeof(text), "%d", g_now.strength);
    WritePrivateProfileStringA("example", "strength", text, g_ini);
    snprintf(text, sizeof(text), "%d", g_now.mode);
    WritePrivateProfileStringA("example", "mode", text, g_ini);
    WritePrivateProfileStringA("example", "enabled", g_now.enabled ? "1" : "0", g_ini);
}

// ---- the rows ----
enum Row { R_STRENGTH, R_MODE, R_ENABLED };

constexpr opt::Row kRows[] = {
    // kind, label, label_hash (0 = use the label), help text
    // kSliderRow draws the game's slider bar, filled to `fill(row)`; `value` is then only the fallback
    {opt::kSliderRow, "Strength", 0, "How strong the effect is. Hold Shift or LB for finer steps."},
    {opt::kValueRow, "Mode", 0, "Picks one of three modes."},
    // The label is the game's own string "Vibration"
    {opt::kValueRow, "", 0xDDDC5E1B, "Turns the example on or off."},
};

constexpr unsigned kGameOn = 0x417B2604, kGameOff = 0x70DFE5C2;  // the game's own "On" and "Off"

unsigned value(int row, char *out, unsigned size) {
    switch (row) {
        case R_STRENGTH: snprintf(out, size, "%d%%", g_now.strength); return 0;
        case R_MODE: snprintf(out, size, "%s", kModes[g_now.mode]); return 0;
        default: return g_now.enabled ? kGameOn : kGameOff;  // a language hash of the game: `out` is ignored
    }
}

float fill(int row) { return row == R_STRENGTH ? g_now.strength / 100.0f : 0.0f; }

void change(int row, int direction, bool fine) {
    switch (row) {
        case R_STRENGTH:
            g_now.strength += direction * (fine ? 1 : 5);
            if (g_now.strength < 0) g_now.strength = 0;
            if (g_now.strength > 100) g_now.strength = 100;
            break;
        case R_MODE:
            g_now.mode += direction;
            if (g_now.mode < 0) g_now.mode = 0;
            if (g_now.mode > 2) g_now.mode = 2;
            break;
        default: g_now.enabled = direction > 0; break;
    }
}

// ---- the page's life ----
void opened() { g_opened = g_now; }
bool changed() { return g_now.strength != g_opened.strength || g_now.mode != g_opened.mode || g_now.enabled != g_opened.enabled; }
void accept() {
    save();
    g_opened = g_now;
}
void discard() { g_now = g_opened; }
void defaults() { g_now = Settings(); }

opt::Page g_page;

// ---- entries in the pause menu and the main menu (kPauseMenuEntry...): the same rows, one page per placement ----
bool pause_entry_available() { return GetPrivateProfileIntA("example", "hide_pause_entry", 0, g_ini) == 0; }

struct MenuTest {
    const char *id, *entry, *title, *help_title;
    opt::Placement placement;
    bool (*available)();
};
constexpr MenuTest kMenuTests[] = {
    {"example_pause", "Pause Menu Test", "PAUSE MENU TEST", "PAUSE MENU TEST INFO", opt::kPauseMenuEntry, pause_entry_available},
    {"example_main", "Main Menu Test", "MAIN MENU TEST", "MAIN MENU TEST INFO", opt::kMainMenuEntry, nullptr},
    {"example_both", "Both Menus Test", "BOTH MENUS TEST", "BOTH MENUS TEST INFO", opt::kPauseAndMainMenuEntry, nullptr},
};
opt::Page g_menu_pages[sizeof(kMenuTests) / sizeof(kMenuTests[0])];

}  // namespace

extern "C" __declspec(dllexport) void nfsmw_custom_menus_provide(const opt::Host *host) {
    load();
    char translations[MAX_PATH];
    nfsmw::data_file("translations\\example_options_page", translations, sizeof(translations));
    host->register_translations(translations);
    g_page = {"example", "Example Page", "EXAMPLE PAGE", "EXAMPLE PAGE INFO",
              /* icon hash */ 0, /* fallback icon hash, 0 = the Options icon */ 0, /* .tpk with the icon, or null */ nullptr,
              static_cast<int>(sizeof(kRows) / sizeof(kRows[0])), kRows,
              /* available */ nullptr, opened, value, change, changed, accept, discard, defaults,
              opt::kNewEntryEverywhere, fill};
    host->register_page(&g_page);
    for (size_t i = 0; i < sizeof(kMenuTests) / sizeof(kMenuTests[0]); ++i) {
        const MenuTest &t = kMenuTests[i];
        g_menu_pages[i] = {t.id, t.entry, t.title, t.help_title, 0, 0, nullptr,
                           static_cast<int>(sizeof(kRows) / sizeof(kRows[0])), kRows,
                           t.available, opened, value, change, changed, accept, discard, defaults,
                           t.placement, fill};
        host->register_page(&g_menu_pages[i]);
    }
}

NFSMW_PLUGIN_MAIN() { return NFSMW_OK; }

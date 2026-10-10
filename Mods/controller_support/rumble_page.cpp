#include "custom_menus_api.h"
#include "rumble.h"
#include "rumble_page.h"

namespace pad {
namespace opt = nfsmw::custom_menus;
// ------------------------------------------------------------------------------------------------
// Pause > Options > Rumble: a page of nfsmw_custom_menus (which builds the screen and asks for the page when the Options
// menu first opens). Row 0 is the game's own Vibration setting, then one row per effect with its level, 0-100. Changing
// a level plays a sample of the effect at the new level, through the same path the game uses.
// ------------------------------------------------------------------------------------------------
namespace {

constexpr unsigned kControlsIconTexture = 0xA04A7B26;  // OPTIONS_ICON_CONTROLS: until there is an OPTIONS_RUMBLE
constexpr unsigned kVibrationText = 0xDDDC5E1B;            // "Vibration": the game's name for the Rumble option
constexpr unsigned kOnText = 0x417B2604, kOffText = 0x70DFE5C2;
constexpr uintptr_t kFEDatabase = 0x91CF90, kPlayerToEdit = 0x91CA24;

// The edited player's PlayerSettings (cFrontendDatabase::GetPlayerSettings: 0x14 bytes each from the options settings
// +0x80); +0x4 is Rumble, the Vibration option. The game's own Options screens compare and restore it with the rest.
unsigned char *edited_player_settings() {
    unsigned char *options = static_cast<unsigned char *>(field<void *>(*reinterpret_cast<void **>(kFEDatabase), 0x10));
    return options + 0x80 + *reinterpret_cast<int *>(kPlayerToEdit) * 0x14;
}
bool vibration_option() { return edited_player_settings()[0x4] != 0; }

int g_opened_levels[rumble::E_COUNT];  // the levels when the page opened: what was last accepted (and is in the ini)

bool page_available() { return rumble::enabled(); }

void page_opened() {
    for (int e = 0; e < rumble::E_COUNT; ++e) g_opened_levels[e] = static_cast<int>(lroundf(rumble::g_level[e] * 100.0f));
}

unsigned page_value(int row, char *out, unsigned out_size) {
    if (row == 0) return vibration_option() ? kOnText : kOffText;
    const int percent = static_cast<int>(lroundf(rumble::g_level[row - 1] * 100.0f));
    if (percent > 0) snprintf(out, out_size, "%d%%", percent);
    else snprintf(out, out_size, "Off");
    return 0;
}

void page_change(int row, int direction, bool fine) {
    if (row == 0) {
        const bool on = direction > 0;
        if (vibration_option() == on) return;
        edited_player_settings()[0x4] = on;
        log_line("Vibration %s", on ? "on" : "off");
        if (on) rumble::play_preview(0);  // as the console version does when Vibration is turned on
        return;
    }
    const int effect = row - 1;
    int percent = static_cast<int>(lroundf(rumble::g_level[effect] * 100.0f)) + direction * (fine ? 1 : 5);
    percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    rumble::g_level[effect] = percent / 100.0f;
    log_line("rumble: %s set to %d", rumble::kEffects[effect].key, percent);
    if (vibration_option() && percent > 0) rumble::play_preview(effect);
}

bool page_changed() {
    for (int e = 0; e < rumble::E_COUNT; ++e)
        if (static_cast<int>(lroundf(rumble::g_level[e] * 100.0f)) != g_opened_levels[e]) return true;
    return false;
}

void page_accept() {
    rumble::save_levels();
    page_opened();
}

void page_discard() {
    for (int e = 0; e < rumble::E_COUNT; ++e) rumble::g_level[e] = g_opened_levels[e] / 100.0f;
}

void page_defaults() {
    for (int e = 0; e < rumble::E_COUNT; ++e) rumble::g_level[e] = rumble::kEffects[e].def / 100.0f;
    edited_player_settings()[0x4] = 1;
}

opt::Row g_rows[1 + rumble::E_COUNT];
char g_help[rumble::E_COUNT][400];
char g_pack_path[MAX_PATH];
opt::Page g_page;

}  // namespace

}  // namespace pad

void pad::rumble_page::provide(const nfsmw::custom_menus::Host *host) {
    using namespace pad;
    g_rows[0] = {opt::kValueRow, "Vibration", kVibrationText, "Turns rumble on or off. This is the game's own "
                 "Vibration setting, kept in your profile."};
    for (int e = 0; e < rumble::E_COUNT; ++e) {
        snprintf(g_help[e], sizeof(g_help[e]), "%s Hold Shift for finer steps.", rumble::kEffects[e].help);
        g_rows[1 + e] = {opt::kValueRow, rumble::kEffects[e].label, 0, g_help[e]};
    }
    nfsmw::data_file("nfsmw_controller_support.tpk", g_pack_path, sizeof(g_pack_path));
    g_page = {"rumble", "Rumble", "RUMBLE", "RUMBLE INFO", nfsmw::texture_hash("OPTIONS_RUMBLE"),
              kControlsIconTexture,  // the Controller icon until there is an OPTIONS_RUMBLE
              g_pack_path, 1 + rumble::E_COUNT, g_rows, page_available, page_opened, page_value,
              page_change, page_changed, page_accept, page_discard, page_defaults};
    host->register_page(&g_page);
}

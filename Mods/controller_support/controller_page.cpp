#include <algorithm>

#include "controller_page.h"
#include "custom_menus_api.h"
#include "device_settings.h"
#include "rumble.h"

namespace pad {
namespace opt = nfsmw::custom_menus;
// ------------------------------------------------------------------------------------------------
// Controller Options: a page of nfsmw_custom_menus (which builds the screen and asks for the page when the Options menu first opens),
// reached from the Controls screen's hint (controls_options.cpp) for the selected controller. Three sliders for the dead zones of
// its sticks and triggers (device_settings.h), then, with rumble, the game's own Vibration setting and one slider per effect with
// its level, 0-100. Changing a level plays a sample of the effect at the new level, through the same path the game uses.
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

// The rows: the three dead zones, then (when rumble works) Vibration and the effects.
enum DeadzoneRow { kLeftStick, kRightStick, kTrigger, kDeadzoneRows };
bool g_rumble_rows = false;  // decided when the page is registered
int vibration_row() { return kDeadzoneRows; }
int effect_of_row(int row) { return row - kDeadzoneRows - 1; }  // negative: not an effect

// Everything is the edited controller's (rumble::edit_slot); each XInput controller has its own.
int percent_of(int effect) { return static_cast<int>(lroundf(rumble::level(rumble::edit_slot(), effect) * 100.0f)); }
float &deadzone_of(int row) {
    device_settings::Deadzones &d = device_settings::deadzones(rumble::edit_slot());
    return row == kLeftStick ? d.left_stick : row == kRightStick ? d.right_stick : d.trigger;
}
int deadzone_percent(int row) { return static_cast<int>(lroundf(deadzone_of(row) * 100.0f)); }

int g_opened_levels[rumble::E_COUNT];  // the values when the page opened: what was last accepted (and is in the ini)
device_settings::Deadzones g_opened_deadzones;

bool page_available() { return true; }

void page_opened() {
    for (int e = 0; e < rumble::E_COUNT; ++e) g_opened_levels[e] = percent_of(e);
    g_opened_deadzones = device_settings::deadzones(rumble::edit_slot());
}

unsigned page_value(int row, char *out, unsigned out_size) {
    if (g_rumble_rows && row == vibration_row()) return vibration_option() ? kOnText : kOffText;
    const int percent = row < kDeadzoneRows ? deadzone_percent(row) : percent_of(effect_of_row(row));
    if (percent > 0) snprintf(out, out_size, "%d%%", percent);
    else snprintf(out, out_size, "Off");
    return 0;
}

void page_change(int row, int direction, bool fine) {
    if (row < kDeadzoneRows) {
        const int percent = std::clamp(deadzone_percent(row) + direction * (fine ? 1 : 5), 0,
                                       static_cast<int>(device_settings::kMaxDeadzone * 100));
        deadzone_of(row) = percent / 100.0f;
        log_line("controller %d: %s dead zone set to %d", rumble::edit_slot() + 1,
                 row == kLeftStick ? "left stick" : row == kRightStick ? "right stick" : "trigger", percent);
        return;
    }
    if (row == vibration_row()) {
        const bool on = direction > 0;
        if (vibration_option() == on) return;
        edited_player_settings()[0x4] = on;
        log_line("Vibration %s", on ? "on" : "off");
        if (on) rumble::play_preview(0);  // as the console version does when Vibration is turned on
        return;
    }
    const int effect = effect_of_row(row);
    int percent = percent_of(effect) + direction * (fine ? 1 : 5);
    percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    rumble::set_level(rumble::edit_slot(), effect, percent / 100.0f);
    log_line("rumble: %s set to %d", rumble::kEffects[effect].key, percent);
    if (vibration_option() && percent > 0) rumble::play_preview(effect);
}

float page_fill(int row) {
    if (row < kDeadzoneRows) return deadzone_of(row) / device_settings::kMaxDeadzone;
    return row == vibration_row() ? 0.0f : percent_of(effect_of_row(row)) / 100.0f;
}

bool page_changed() {
    const device_settings::Deadzones &now = device_settings::deadzones(rumble::edit_slot());
    if (now.left_stick != g_opened_deadzones.left_stick || now.right_stick != g_opened_deadzones.right_stick ||
        now.trigger != g_opened_deadzones.trigger)
        return true;
    for (int e = 0; e < rumble::E_COUNT; ++e)
        if (percent_of(e) != g_opened_levels[e]) return true;
    return false;
}

void page_accept() {
    device_settings::save(rumble::edit_slot());
    rumble::save_levels(rumble::edit_slot());
    page_opened();
}

void page_discard() {
    device_settings::deadzones(rumble::edit_slot()) = g_opened_deadzones;
    for (int e = 0; e < rumble::E_COUNT; ++e) rumble::set_level(rumble::edit_slot(), e, g_opened_levels[e] / 100.0f);
}

void page_defaults() {
    device_settings::deadzones(rumble::edit_slot()) = device_settings::Deadzones{};
    for (int e = 0; e < rumble::E_COUNT; ++e) rumble::set_level(rumble::edit_slot(), e, rumble::kEffects[e].def / 100.0f);
    if (g_rumble_rows) edited_player_settings()[0x4] = 1;
}

opt::Row g_rows[kDeadzoneRows + 1 + rumble::E_COUNT];
char g_help[rumble::E_COUNT][400];
char g_pack_path[MAX_PATH];
opt::Page g_page;

}  // namespace

}  // namespace pad

void pad::controller_page::provide(const nfsmw::custom_menus::Host *host) {
    using namespace pad;
    g_rumble_rows = rumble::enabled();
    g_rows[kLeftStick] = {opt::kSliderRow, "Left Stick Dead Zone", 0, "The left stick ignores movements smaller than this. "
                          "Raise it if the car or the menu cursor drifts with the stick at rest. Hold Shift or LB for finer steps."};
    g_rows[kRightStick] = {opt::kSliderRow, "Right Stick Dead Zone", 0, "The right stick ignores movements smaller than this. "
                           "Raise it if the camera drifts with the stick at rest. Hold Shift or LB for finer steps."};
    g_rows[kTrigger] = {opt::kSliderRow, "Trigger Dead Zone", 0, "The triggers ignore pulls lighter than this. "
                        "Hold Shift or LB for finer steps."};
    int rows = kDeadzoneRows;
    if (g_rumble_rows) {
        g_rows[rows++] = {opt::kValueRow, "Vibration", kVibrationText, "Turns rumble on or off. This is the game's own "
                          "Vibration setting, kept in your profile."};
        for (int e = 0; e < rumble::E_COUNT; ++e) {
            snprintf(g_help[e], sizeof(g_help[e]), "%s Hold Shift or LB for finer steps.", rumble::kEffects[e].help);
            g_rows[rows++] = {opt::kSliderRow, rumble::kEffects[e].label, 0, g_help[e]};
        }
    }
    nfsmw::data_file("nfsmw_controller_support.tpk", g_pack_path, sizeof(g_pack_path));
    g_page = {"controller", "Controller", "CONTROLLER", "CONTROLLER INFO", nfsmw::texture_hash("OPTIONS_RUMBLE"),
              kControlsIconTexture,  // the Controller icon until there is an OPTIONS_RUMBLE
              g_pack_path, rows, g_rows, page_available, page_opened, page_value,
              page_change, page_changed, page_accept, page_discard, page_defaults, opt::kHidden, page_fill};
    host->register_page(&g_page);
}

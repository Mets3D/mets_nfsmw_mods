#include <algorithm>

#include "controller_page.h"
#include "custom_menus_api.h"
#include "device_settings.h"
#include "rumble.h"

namespace pad {
namespace opt = nfsmw::custom_menus;
// ------------------------------------------------------------------------------------------------
// Settings: a page of nfsmw_custom_menus (which builds the screen and asks for the page when the Options menu first opens),
// reached from the Controls screen's hint (controls_options.cpp) for the selected controller. The style of the button prompts it
// gets, three sliders for the dead zones of its sticks and triggers (device_settings.h), then, with rumble, the game's own Vibration setting and one slider per effect with
// its level, 0-100. Changing a level plays a sample of the effect at the new level, through the same path the game uses.
// ------------------------------------------------------------------------------------------------
namespace {

constexpr unsigned kControlsIconTexture = 0xA04A7B26;  // OPTIONS_ICON_CONTROLS: until there is an OPTIONS_RUMBLE
constexpr unsigned kVibrationText = 0xDDDC5E1B;            // "Vibration": the game's name for the Rumble option
constexpr unsigned kOnText = 0x417B2604, kOffText = 0x70DFE5C2;

// Vibration is the controller's own setting (rumble.h), not the game's player setting (which nothing in the PC menus changes).
bool vibration_option() { return rumble::vibration(rumble::edit_slot()); }

// The rows: the three dead zones, then (when rumble works) Vibration and the effects.
enum FixedRow { kPrompts, kLeftStick, kRightStick, kTrigger, kFixedRows };
bool g_rumble_rows = false;  // decided when the page is registered
bool is_deadzone(int row) { return row >= kLeftStick && row <= kTrigger; }
int vibration_row() { return kFixedRows; }
int effect_of_row(int row) { return row - kFixedRows - 1; }  // negative: not an effect

// Everything is the edited controller's (rumble::edit_slot); each XInput controller has its own.
int percent_of(int effect) { return static_cast<int>(lroundf(rumble::level(rumble::edit_slot(), effect) * 100.0f)); }
float &deadzone_of(int row) {
    device_settings::Deadzones &d = device_settings::deadzones(rumble::edit_slot());
    return row == kLeftStick ? d.left_stick : row == kRightStick ? d.right_stick : d.trigger;
}
int deadzone_percent(int row) { return static_cast<int>(lroundf(deadzone_of(row) * 100.0f)); }

button_icons::Style &prompts_style() { return device_settings::prompts(rumble::edit_slot()); }

int g_opened_levels[rumble::E_COUNT];  // the values when the page opened: what was last accepted (and is in the ini)
device_settings::Deadzones g_opened_deadzones;
button_icons::Style g_opened_prompts;
bool g_opened_vibration = true;

bool page_available() { return true; }

void page_opened() {
    for (int e = 0; e < rumble::E_COUNT; ++e) g_opened_levels[e] = percent_of(e);
    g_opened_deadzones = device_settings::deadzones(rumble::edit_slot());
    g_opened_prompts = prompts_style();
    g_opened_vibration = g_rumble_rows ? vibration_option() : true;
}

unsigned page_value(int row, char *out, unsigned out_size) {
    if (row == kPrompts) {
        const button_icons::Style style = prompts_style();
        snprintf(out, out_size, "%s", style == button_icons::Style::kXbox ? "Xbox" : style == button_icons::Style::kPlayStation ? "PlayStation" : style == button_icons::Style::kSwitch ? "Switch" : style == button_icons::Style::kSwitchNintendoLayout ? "Switch (Nintendo Layout)" : "Off");
        return 0;
    }
    if (g_rumble_rows && row == vibration_row()) return vibration_option() ? kOnText : kOffText;
    const int percent = is_deadzone(row) ? deadzone_percent(row) : percent_of(effect_of_row(row));
    if (percent > 0) snprintf(out, out_size, "%d%%", percent);
    else snprintf(out, out_size, "Off");
    return 0;
}

void page_change(int row, int direction, bool fine) {
    if (row == kPrompts) {  // Xbox, PlayStation, Switch, Switch (Nintendo Layout): the arrows go round (from Off, to Xbox)
        using button_icons::Style;
        constexpr Style kStyles[] = {Style::kXbox, Style::kPlayStation, Style::kSwitch, Style::kSwitchNintendoLayout};
        constexpr int kCount = 4;
        int i = 0;
        while (i < kCount && kStyles[i] != prompts_style()) ++i;
        prompts_style() = i == kCount ? Style::kXbox : kStyles[(i + (direction > 0 ? 1 : kCount - 1)) % kCount];
        log_line("controller %d: button prompts set to %s", rumble::edit_slot() + 1, button_icons::style_text(prompts_style()));
        return;
    }
    if (is_deadzone(row)) {
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
        rumble::set_vibration(rumble::edit_slot(), on);
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
    if (row == kPrompts) return 0.0f;
    if (is_deadzone(row)) return deadzone_of(row) / device_settings::kMaxDeadzone;
    return row == vibration_row() ? 0.0f : percent_of(effect_of_row(row)) / 100.0f;
}

bool page_changed() {
    const device_settings::Deadzones &now = device_settings::deadzones(rumble::edit_slot());
    if (now.left_stick != g_opened_deadzones.left_stick || now.right_stick != g_opened_deadzones.right_stick ||
        now.trigger != g_opened_deadzones.trigger || prompts_style() != g_opened_prompts ||
        (g_rumble_rows && vibration_option() != g_opened_vibration))
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
    prompts_style() = g_opened_prompts;
    if (g_rumble_rows) rumble::set_vibration(rumble::edit_slot(), g_opened_vibration);
    for (int e = 0; e < rumble::E_COUNT; ++e) rumble::set_level(rumble::edit_slot(), e, g_opened_levels[e] / 100.0f);
}

void page_defaults() {
    device_settings::deadzones(rumble::edit_slot()) = device_settings::Deadzones{};
    prompts_style() = button_icons::Style::kXbox;
    for (int e = 0; e < rumble::E_COUNT; ++e) rumble::set_level(rumble::edit_slot(), e, rumble::kEffects[e].def / 100.0f);
    if (g_rumble_rows) rumble::set_vibration(rumble::edit_slot(), true);
}

// The live bar under a dead zone slider: how far the edited controller's stick or trigger is pushed now, on the slider's scale (0 to
// kMaxDeadzone of full travel), from the raw state, as the dead zone sees it. A stick is judged by its distance from the centre.
float page_live(int row) {
    if (!is_deadzone(row)) return -1.0f;
    xinput::Gamepad pad = {};
    if (!xinput::read_slot(rumble::edit_slot(), &pad)) return -1.0f;
    float pushed;
    if (row == kTrigger) {
        pushed = std::max(pad.left_trigger, pad.right_trigger) / 255.0f;
    } else {
        const float x = (row == kLeftStick ? pad.lx : pad.rx) / 32767.0f, y = (row == kLeftStick ? pad.ly : pad.ry) / 32767.0f;
        pushed = std::sqrt(x * x + y * y);
    }
    return std::min(1.0f, pushed / device_settings::kMaxDeadzone);
}

// The rumble levels mean nothing with Vibration off: greyed out.
bool page_row_enabled(int row) { return effect_of_row(row) < 0 || vibration_option(); }

opt::Row g_rows[kFixedRows + 1 + rumble::E_COUNT];
char g_help[rumble::E_COUNT][400];
char g_pack_path[MAX_PATH];
opt::Page g_page;

}  // namespace

}  // namespace pad

void pad::controller_page::provide(const nfsmw::custom_menus::Host *host) {
    using namespace pad;
    g_rumble_rows = rumble::enabled();
    g_rows[kPrompts] = {opt::kValueRow, "Controller Prompts", 0, "Which buttons the menus show while this controller is in use: Xbox, PlayStation or Switch. Switch (Nintendo Layout) is for a Switch controller whose A and B are where its labels say (Steam's Nintendo button layout)."};
    g_rows[kLeftStick] = {opt::kSliderRow, "Left Stick Dead Zone", 0, "The left stick ignores movements smaller than this. "
                          "Raise it if the car or the menu cursor drifts with the stick at rest. Hold Shift or LB for finer steps."};
    g_rows[kRightStick] = {opt::kSliderRow, "Right Stick Dead Zone", 0, "The right stick ignores movements smaller than this. "
                           "Raise it if the camera drifts with the stick at rest. Hold Shift or LB for finer steps."};
    g_rows[kTrigger] = {opt::kSliderRow, "Trigger Dead Zone", 0, "The triggers ignore pulls lighter than this. "
                        "Hold Shift or LB for finer steps."};
    int rows = kFixedRows;
    if (g_rumble_rows) {
        g_rows[rows++] = {opt::kValueRow, "Vibration", kVibrationText, "Enable controller vibration."};
        for (int e = 0; e < rumble::E_COUNT; ++e) {
            snprintf(g_help[e], sizeof(g_help[e]), "%s Hold Shift or LB for finer steps.", rumble::kEffects[e].help);
            g_rows[rows++] = {opt::kSliderRow, rumble::kEffects[e].label, 0, g_help[e]};
        }
    }
    nfsmw::data_file("mets_controller_support.tpk", g_pack_path, sizeof(g_pack_path));
    g_page = {"controller", "Controller", "CONTROLLER", "CONTROLLER INFO", nfsmw::texture_hash("OPTIONS_RUMBLE"),
              kControlsIconTexture,  // the Controller icon until there is an OPTIONS_RUMBLE
              g_pack_path, rows, g_rows, page_available, page_opened, page_value,
              page_change, page_changed, page_accept, page_discard, page_defaults, opt::kHidden, page_fill, page_row_enabled, page_live};
    host->register_page(&g_page);
}

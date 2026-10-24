#include "device_settings.h"

#include "input.h"
#include "profile.h"
#include "rumble.h"
#include "steam_pads.h"

namespace pad::device_settings {

namespace {

Deadzones g_deadzones[kSlots];
button_icons::Style g_prompts[kSlots];
char g_keys[kSlots][48] = {"1", "2", "3", "4"};  // key_of
char g_profile[MAX_PATH];                          // the profile file the slots were loaded from ("": none)

// A controller's values are in [controller_<id>] of the profile's controller_settings.ini; a key missing there takes the same section of
// the mod's ini, then [pad] / [buttons].
void section_of(int slot, char *out, size_t n) { snprintf(out, n, "controller_%s", g_keys[slot]); }

// The prompts a controller starts with when none were chosen for it: those of its kind (Steam's word for it, or the vendor), else [buttons].
button_icons::Style suggested_style(int slot) {
    steam_pads::RealDevice device;
    if (!steam_pads::device_of_slot(slot, &device)) return button_icons::default_style();
    const char *type = device.type;
    if (!_strnicmp(type, "ps", 2)) return button_icons::Style::kPlayStation;
    if (!_strnicmp(type, "switch", 6) || !_strnicmp(type, "joycon", 6)) return button_icons::Style::kSwitch;
    if (!_strnicmp(type, "xbox", 4)) return button_icons::Style::kXbox;
    return button_icons::default_style();
}

float clamp_deadzone(float x) { return x < 0.0f ? 0.0f : x > kMaxDeadzone ? kMaxDeadzone : x; }

}  // namespace

int clamp_slot(int slot) { return slot >= 0 && slot < kSlots ? slot : 0; }
Deadzones &deadzones(int slot) { return g_deadzones[clamp_slot(slot)]; }
button_icons::Style &prompts(int slot) { return g_prompts[clamp_slot(slot)]; }

namespace {

Deadzones g_base;  // for a controller with no key: [pad]

void load_slot(int slot) {
    char section[64];
    section_of(slot, section, sizeof(section));
    Deadzones &d = g_deadzones[slot];
    d.left_stick = clamp_deadzone(profile::get_float(section, "left_stick_deadzone", g_base.left_stick));
    d.right_stick = clamp_deadzone(profile::get_float(section, "right_stick_deadzone", g_base.right_stick));
    d.trigger = clamp_deadzone(profile::get_float(section, "trigger_deadzone", g_base.trigger));
    char text[32] = {};
    profile::get_string(section, "button_prompts", text, sizeof(text));
    g_prompts[slot] = button_icons::parse_style(text, suggested_style(slot));
}

}  // namespace

const char *key_of(int slot) { return g_keys[clamp_slot(slot)]; }

void refresh() {
    static DWORD next = 0;
    const DWORD now = GetTickCount();
    if (static_cast<LONG>(now - next) < 0) return;
    next = now + 1000;
    const bool new_profile = strcmp(profile::file(), g_profile) != 0;
    if (new_profile) {
        snprintf(g_profile, sizeof(g_profile), "%s", profile::file());
        log_line("dead zones, button prompts and rumble are read from %s", g_profile[0] ? g_profile : "the mod's ini (the profile has no folder yet)");
    }
    for (int slot = 0; slot < kSlots; ++slot) {
        char key[48];
        if (!input::identity_of_slot(slot, key, sizeof(key))) snprintf(key, sizeof(key), "%d", slot + 1);
        if (!new_profile && !strcmp(key, g_keys[slot])) continue;
        if (strcmp(key, g_keys[slot])) {
            char what[200];
            steam_pads::describe(slot, what, sizeof(what));
            log_line("XInput slot %d: the controller is now %s (was %s): %s", slot + 1, key, g_keys[slot], what);
        }
        snprintf(g_keys[slot], sizeof(g_keys[slot]), "%s", key);
        load_slot(slot);
        rumble::reload_slot(slot);
    }
}

void load() {
    g_base = {clamp_deadzone(ini_float("left_stick_deadzone", Deadzones{}.left_stick)),
              clamp_deadzone(ini_float("right_stick_deadzone", Deadzones{}.right_stick)),
              clamp_deadzone(ini_float("trigger_deadzone", Deadzones{}.trigger))};
    snprintf(g_profile, sizeof(g_profile), "%s", profile::file());
    for (int slot = 0; slot < kSlots; ++slot) load_slot(slot);
    const Deadzones &d = g_deadzones[0];
    log_line("controller 1: dead zones left stick %.2f, right stick %.2f, triggers %.2f; button prompts %s", d.left_stick,
             d.right_stick, d.trigger, button_icons::style_text(g_prompts[0]));
}

void save(int slot) {
    slot = clamp_slot(slot);
    char section[64];
    section_of(slot, section, sizeof(section));
    const Deadzones &d = g_deadzones[slot];
    const struct { const char *key; float value; } keys[] = {
        {"left_stick_deadzone", d.left_stick}, {"right_stick_deadzone", d.right_stick}, {"trigger_deadzone", d.trigger}};
    for (const auto &k : keys) {
        char value[16];
        snprintf(value, sizeof(value), "%.2f", k.value);
        profile::set(section, k.key, value);
    }
    profile::set(section, "button_prompts", button_icons::style_text(g_prompts[slot]));
    log_line("controller %d (%s): settings saved to %s (dead zones left stick %.2f, right stick %.2f, triggers %.2f; button prompts %s)", slot + 1, g_keys[slot],
             profile::file()[0] ? profile::file() : "the mod's ini", d.left_stick, d.right_stick, d.trigger, button_icons::style_text(g_prompts[slot]));
}

}  // namespace pad::device_settings

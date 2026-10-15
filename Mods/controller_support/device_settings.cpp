#include "device_settings.h"

#include "input.h"
#include "rumble.h"

namespace pad::device_settings {

namespace {

Deadzones g_deadzones[kSlots];
button_icons::Style g_prompts[kSlots];
char g_keys[kSlots][48] = {"1", "2", "3", "4"};  // key_of

void section_of(int slot, char *out, size_t n) { snprintf(out, n, "controller_%s", g_keys[slot]); }

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
    d.left_stick = clamp_deadzone(ini_float("left_stick_deadzone", g_base.left_stick, section));
    d.right_stick = clamp_deadzone(ini_float("right_stick_deadzone", g_base.right_stick, section));
    d.trigger = clamp_deadzone(ini_float("trigger_deadzone", g_base.trigger, section));
    char text[32] = {};
    GetPrivateProfileStringA(section, "button_prompts", "", text, sizeof(text), g_ini);
    g_prompts[slot] = button_icons::parse_style(text, button_icons::default_style());
}

}  // namespace

const char *key_of(int slot) { return g_keys[clamp_slot(slot)]; }

void refresh() {
    static DWORD next = 0;
    const DWORD now = GetTickCount();
    if (static_cast<LONG>(now - next) < 0) return;
    next = now + 1000;
    for (int slot = 0; slot < kSlots; ++slot) {
        char key[48];
        if (!input::identity_of_slot(slot, key, sizeof(key))) snprintf(key, sizeof(key), "%d", slot + 1);
        if (!strcmp(key, g_keys[slot])) continue;
        log_line("XInput slot %d: the controller is now %s (was %s)", slot + 1, key, g_keys[slot]);
        snprintf(g_keys[slot], sizeof(g_keys[slot]), "%s", key);
        load_slot(slot);
        rumble::reload_slot(slot);
    }
}

void load() {
    g_base = {clamp_deadzone(ini_float("left_stick_deadzone", Deadzones{}.left_stick)),
              clamp_deadzone(ini_float("right_stick_deadzone", Deadzones{}.right_stick)),
              clamp_deadzone(ini_float("trigger_deadzone", Deadzones{}.trigger))};
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
        WritePrivateProfileStringA(section, k.key, value, g_ini);
    }
    WritePrivateProfileStringA(section, "button_prompts", button_icons::style_text(g_prompts[slot]), g_ini);
    log_line("controller %d (%s): settings saved (dead zones left stick %.2f, right stick %.2f, triggers %.2f; button prompts %s)", slot + 1, g_keys[slot],
             d.left_stick, d.right_stick, d.trigger, button_icons::style_text(g_prompts[slot]));
}

}  // namespace pad::device_settings

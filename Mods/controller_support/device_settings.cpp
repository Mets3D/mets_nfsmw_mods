#include "device_settings.h"

namespace pad::device_settings {

namespace {

Deadzones g_deadzones[kSlots];

void section_of(int slot, char *out, size_t n) { snprintf(out, n, "controller_%d", slot + 1); }

float clamp_deadzone(float x) { return x < 0.0f ? 0.0f : x > kMaxDeadzone ? kMaxDeadzone : x; }

}  // namespace

int clamp_slot(int slot) { return slot >= 0 && slot < kSlots ? slot : 0; }
Deadzones &deadzones(int slot) { return g_deadzones[clamp_slot(slot)]; }

void load() {
    const Deadzones base = {clamp_deadzone(ini_float("left_stick_deadzone", Deadzones{}.left_stick)),
                            clamp_deadzone(ini_float("right_stick_deadzone", Deadzones{}.right_stick)),
                            clamp_deadzone(ini_float("trigger_deadzone", Deadzones{}.trigger))};
    for (int slot = 0; slot < kSlots; ++slot) {
        char section[16];
        section_of(slot, section, sizeof(section));
        Deadzones &d = g_deadzones[slot];
        d.left_stick = clamp_deadzone(ini_float("left_stick_deadzone", base.left_stick, section));
        d.right_stick = clamp_deadzone(ini_float("right_stick_deadzone", base.right_stick, section));
        d.trigger = clamp_deadzone(ini_float("trigger_deadzone", base.trigger, section));
    }
    const Deadzones &d = g_deadzones[0];
    log_line("dead zones, controller 1: left stick %.2f, right stick %.2f, triggers %.2f", d.left_stick, d.right_stick, d.trigger);
}

void save(int slot) {
    slot = clamp_slot(slot);
    char section[16];
    section_of(slot, section, sizeof(section));
    const Deadzones &d = g_deadzones[slot];
    const struct { const char *key; float value; } keys[] = {
        {"left_stick_deadzone", d.left_stick}, {"right_stick_deadzone", d.right_stick}, {"trigger_deadzone", d.trigger}};
    for (const auto &k : keys) {
        char value[16];
        snprintf(value, sizeof(value), "%.2f", k.value);
        WritePrivateProfileStringA(section, k.key, value, g_ini);
    }
    log_line("controller %d: dead zones saved (left stick %.2f, right stick %.2f, triggers %.2f)", slot + 1, d.left_stick,
             d.right_stick, d.trigger);
}

}  // namespace pad::device_settings

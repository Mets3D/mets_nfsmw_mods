// Settings of each XInput controller (slot 0-3) that the Controller Options page edits: the dead zones. They are kept in
// [controller_1] .. [controller_4] of the ini; a controller with no key takes [pad] left_stick_deadzone / right_stick_deadzone, or
// the default here.
#pragma once

#include "pad.h"

namespace pad::device_settings {

constexpr int kSlots = 4;
constexpr float kMaxDeadzone = 0.5f;

struct Deadzones {
    float left_stick = 0.15f;   // 0..kMaxDeadzone: how far a stick or trigger has to move before it counts
    float right_stick = 0.15f;
    float trigger = 0.05f;
};

void load();  // from the ini
void save(int slot);
Deadzones &deadzones(int slot);  // slots outside 0-3 are slot 0's
int clamp_slot(int slot);

}  // namespace pad::device_settings

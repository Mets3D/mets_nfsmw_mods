// Settings of each XInput controller that the Settings page edits: the dead zones, the style of the button prompts (and the rumble
// levels, rumble.h). They belong to the controller, not to the slot it is in: a controller that can be told apart from others
// (input.h identity_of_slot) keeps them in [controller_<vid>_<pid>_<serial>], any other in [controller_1] .. [controller_4] by slot. The
// arrays here are those of the controller in each slot (slot 0-3) now; refresh() reloads a slot's when another controller takes it.
// A controller with no key takes [pad] left_stick_deadzone / right_stick_deadzone / trigger_deadzone and [buttons] button_prompts,
// or the default here.
#pragma once

#include "button_icons.h"
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
// Which controller is in each slot, looked at again once a second at most: a slot whose controller changed gets that controller's
// settings (and rumble levels). Called from the game's polling of the controllers.
void refresh();
// What names the controller in a slot in the ini: "045E_02DD_<serial>", or "1" .. "4" when nothing tells it from the others.
const char *key_of(int slot);
void save(int slot);
Deadzones &deadzones(int slot);  // slots outside 0-3 are slot 0's
button_icons::Style &prompts(int slot);  // which buttons the menus show while this controller is the one in use
int clamp_slot(int slot);

}  // namespace pad::device_settings

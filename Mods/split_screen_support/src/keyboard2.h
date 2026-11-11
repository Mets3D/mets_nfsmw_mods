// Keyboard 2: a second player on the same keyboard ([options] simulate_two_keyboards), with keys of its own: the names in the ini's [keyboard_2] are the
// defaults, what the Controls screen changed is in [keyboard_2_bindings]. players.cpp gives the device to whoever lets go of Numpad Enter on a Press
// Start screen, player_devices.cpp reads its keys for that player. With mets_controller_support its keys are a page of the Controls screen
// (common/controller_api.h SecondKeyboard).
#pragma once

namespace split_screen_support::keyboard2 {

constexpr char kName[] = "Keyboard 2";

void install();
bool enabled();
void actions(const unsigned char *keyboard, float *values);  // what its keys ask for, per binding entry; `keyboard` as player_devices::read_keyboard gives it
bool uses_key(int key);                                      // a key (DirectInput code) that is its own while it plays

}  // namespace split_screen_support::keyboard2

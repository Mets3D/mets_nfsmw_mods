// What the game reads from the controller (DirectInput) and how it maps it to actions.
#pragma once

#include "pad.h"

namespace pad::input {

// Hooks DirectInput's GetDeviceState. Done on the game's first input poll rather than at load time, so DirectInput is
// never started from inside the loader.
void hook_dinput();
bool any_xinput_device();        // an XInput controller has been seen
bool only_xinput_controllers();  // every DirectInput joystick seen is an XInput controller
bool no_pads();                  // another mod asked for no controller to reach the game
int mouse_player();               // whose camera the mouse looks around in: 0 the first player, 1 the second
int second_player_slot();        // the XInput controller of the second player (their rumble, their camera look), or -1
// The last thing the player touched was a controller (not the keyboard or the mouse).
bool last_input_was_pad();
// The mouse movement since the last call (the camera look reads it).
void take_mouse_delta(LONG *dx, LONG *dy);
void install_hooks();  // InputMapping

}  // namespace pad::input

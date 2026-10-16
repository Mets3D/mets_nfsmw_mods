// Controllers connected after the game started. The game lists its DirectInput controllers once, at start-up (GameDevice::GlobalInit
// asks DirectInput for its devices and keeps what it finds in the joystick table, 0x91F150); a controller connected later is not in
// it, so the Controls screen does not show it, the controllers do not keep bindings of their own, and a game started with no controller
// has none to read at all. Here the game is handed the new controllers the way it got the first ones, so that it is in the state a
// restart would have put it in.
#pragma once

#include "pad.h"

namespace pad::new_controllers {

void check();          // every 2 seconds: asks DirectInput for controllers the game does not have, and gives them to it (game thread)
void install_hooks();  // remembers the profile's saved bindings, for a controller that arrives after the profile was loaded

}  // namespace pad::new_controllers

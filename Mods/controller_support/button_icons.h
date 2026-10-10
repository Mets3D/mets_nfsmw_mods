// The button pictures in the game's menus and HUD ("Enter", "Esc", "Q", ...): they show the controller's buttons while
// the controller was the last thing used, and the keyboard keys otherwise.
#pragma once

#include "pad.h"

namespace pad::button_icons {

void load_settings();  // [buttons] icons
void install_hooks();

}  // namespace pad::button_icons

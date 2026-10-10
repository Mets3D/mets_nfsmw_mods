// Which controller buttons the menus use: A accepts, B goes back, Y is the "1" action (in vanilla it is B, X and A).
#pragma once

#include "pad.h"

namespace pad::menu_buttons {

void load_settings();        // [buttons] menu_layout
void patch_default_table();  // the game's default bindings, at start-up
void apply_live();           // the live bindings, on every poll (cheap: it writes only what is different)

}  // namespace pad::menu_buttons

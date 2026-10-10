// Which controller buttons the menus use: A accepts, B goes back, Y, X and View are the "1", "2" and "3" hotkeys of the
// menus (in vanilla the controller has no buttons for the last two), and B quits from the main menu, where there is no
// screen to go back to.
#pragma once

#include "pad.h"

namespace pad::menu_buttons {

void load_settings();        // [buttons] menu_layout
void patch_default_table();  // the game's default bindings, at start-up
void apply_live();           // the live bindings, on every poll (cheap: it writes only what is different)
// The buttons of an XInput controller (DirectInput numbering: A B X Y LB RB View Menu), on every
// poll: presses the menus' hotkeys.
void pad_buttons(const unsigned char buttons[32]);

}  // namespace pad::menu_buttons

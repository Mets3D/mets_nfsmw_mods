// Whether the Controls screen asks about changed bindings: [controls] confirm_keybinds=1 asks "Save changes?" (Yes / No) when the
// screen is left with Back and when the device is switched (shoulder buttons, the Settings hint); 0 (the default) never asks and keeps them.
#pragma once

#include "pad.h"

namespace pad::confirm_changes {

void load_settings();

// The Controls screen's message handler, before the game's: the message the game should get, or 0 for none.
unsigned filter_message(void *screen, unsigned message);

}  // namespace pad::confirm_changes

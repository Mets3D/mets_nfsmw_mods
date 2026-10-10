// The button pictures in the game's menus and HUD ("Enter", "Esc", "Q", ...): they show the controller's buttons while
// the controller was the last thing used, and the keyboard keys otherwise.
#pragma once

#include "pad.h"

namespace pad::button_icons {

void load_settings();  // [buttons] icons
void install_hooks();  // also watches the prompts on screen (for quit_prompt_shown), whatever the icons setting
// A "Quit" prompt (the key Q) is on screen and no "Back" prompt: the main menu, where B has nothing to go back to.
bool quit_prompt_shown();

}  // namespace pad::button_icons

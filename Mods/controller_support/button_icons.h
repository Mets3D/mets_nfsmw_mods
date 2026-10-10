// The button pictures in the game's menus and HUD ("Enter", "Esc", "Q", ...): they show the controller's buttons while
// the controller was the last thing used, and the keyboard keys otherwise.
#pragma once

#include "pad.h"

namespace pad::button_icons {

// Which controller's buttons the prompts show (the setting is `icons=` in the ini, and Options > Player > Controller Prompts).
enum class Style { kOff, kXbox, kPlayStation };
Style style();
void set_style(Style style);  // takes effect at once
void save_style();            // to the ini

void load_settings();  // [buttons] icons
void install_hooks();  // also watches the prompts on screen (for quit_prompt_shown), whatever the icons setting
// A "Quit" prompt (the key Q) is on screen and no "Back" prompt: the main menu, where B has nothing to go back to.
bool quit_prompt_shown();

}  // namespace pad::button_icons

// The button pictures in the game's menus and HUD ("Enter", "Esc", "Q", ...): they show the buttons of the controller that was the
// last thing used (in the style set for that controller, device_settings.h), and the keyboard keys otherwise.
#pragma once

#include "pad.h"

namespace pad::button_icons {

// Which controller's buttons the prompts show: kOff keeps the keyboard keys.
// kSwitch labels the buttons by position, as the other styles do (Accept is the Switch's B, the bottom button). kSwitchNintendoLayout is for a Switch controller whose
// buttons already arrive as printed (Steam's "Use Nintendo Button Layout", on by default for them): Accept is its A, the right button.
enum class Style { kOff, kXbox, kPlayStation, kSwitch, kSwitchNintendoLayout };
const char *style_text(Style style);  // "off", "xbox", "playstation", "switch", "switch_nintendo_layout": the ini's words
Style parse_style(const char *text, Style fallback);

// The style of a controller that has none of its own (the ini's [buttons] button_prompts), and of a controller that is not XInput.
Style default_style();
void load_settings();  // [buttons] button_prompts
void install_hooks();  // also watches the prompts on screen (for quit_prompt_shown), whatever the icons setting
// A "Quit" prompt (the key Q) is on screen and no "Back" prompt: the main menu, where B has nothing to go back to.
bool quit_prompt_shown();

}  // namespace pad::button_icons

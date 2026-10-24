// Our opinionated default bindings for the controller (an XInput pad): RT accelerates, LT brakes, ... See default_layout.cpp. They replace
// the game's own in the default table, in the live table at start-up, and whenever the game resets the controller bindings (the
// Controls screen's Defaults, a profile saved with another kind of controller). An existing profile keeps its saved bindings.
#pragma once

#include "custom_menus_api.h"
#include "pad.h"

namespace pad::default_layout {

void load_settings();         // [buttons] better_default_controller_bindings
void patch_default_table();   // at start-up
bool enabled();               // [buttons] better_default_controller_bindings
void suppress(bool on);       // while on, the game's reset of the controller bindings (on_controller_defaults) leaves ours out: the game's own defaults can be read
int apply_to_live();          // ours into the live table now; how many entries
void apply_live();            // the live table, once, on the first input poll (after the game's own start-up defaults)
void provide(const nfsmw::custom_menus::Host *host);  // the callback for the game's defaults being reset

}  // namespace pad::default_layout

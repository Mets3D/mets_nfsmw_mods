// A second keyboard on the Controls screen: another set of keys on the same keyboard, which the mod that provides it keeps and reads (common/controller_api.h
// SecondKeyboard: nfsmw_split_screen_support's Keyboard 2, for a second player on the keyboard). Here it is only a device of the Controls screen, which this
// mod's hooks own: its keys are in the binding table's keyboard slots while the screen shows it, as a controller's set is in pad_sets.cpp.
#pragma once

#include "binding_table.h"
#include "controller_api.h"

namespace pad::keyboard2 {

bool provide(const nfsmw::controller_api::SecondKeyboard *keyboard);  // false when it cannot be had
bool enabled();               // a mod provided one
const char *name();
bool editing();               // the Controls screen shows it: the table's keyboard slots hold its keys
void begin_edit();
void end_edit();              // the keys that were changed go to the mod, the keyboard's own come back
void reset_editor();          // the screen's Defaults
void install_hooks();

}  // namespace pad::keyboard2

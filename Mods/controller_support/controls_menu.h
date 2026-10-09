// Options > Controls: four more rows ("Look Left / Right / Up / Down") at the end of both lists, and direction names
// for the controller bindings.
#pragma once

#include "pad.h"

namespace pad::controls_menu {

void add_rows();       // patches the screen's code to make room for the rows (before the hooks)
bool rows_added();
void install_hooks();  // needs add_rows() to have worked for the row translation

}  // namespace pad::controls_menu

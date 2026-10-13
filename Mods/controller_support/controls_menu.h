// Options > Controls: direction names for the controller bindings (the rows we add to the screen are nfsmw_custom_menus'
// doing: bindings.h provide_controls_rows), and the controller defaults of our entries.
#pragma once

#include "pad.h"

namespace pad::controls_menu {

void binding_label(char *out, int column, int entry, bool primary);  // nfsmw_custom_menus calls it for every label
void install_hooks();

}  // namespace pad::controls_menu

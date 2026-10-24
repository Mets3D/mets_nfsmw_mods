// The strip menus (pause menu, Options, main menu...) cycle: left on the first icon selects the last one, and right on the last the first.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace custom_menus::cycle_strips {

// Installed on its own: `[features] cycling_strip_menus=0` turns it off.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace custom_menus::cycle_strips

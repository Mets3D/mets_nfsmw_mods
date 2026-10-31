// A menu reopened with Back starts on the entry it was left from (the race type menu started on Circuit with Redux's plugins).
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace custom_menus::last_button {

// Installed on its own: nothing else depends on it. `[features] remember_menu_entry=0` turns it off.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace custom_menus::last_button

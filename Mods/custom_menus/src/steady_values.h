// The values of the race options keep their colour: they no longer turn darker once the cursor has been on their row.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace custom_menus::steady_values {

// Installed on its own: nothing else depends on it. `[features] steady_option_values=0` turns it off.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace custom_menus::steady_values

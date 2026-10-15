// Confirmations and notices that always get the same answer are answered right away instead of shown.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace custom_menus::skip_dialogs {

// Installed on its own: nothing else depends on it. `[features] fewer_popups=0` turns it off.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace custom_menus::skip_dialogs

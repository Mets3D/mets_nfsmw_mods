// The movies the game plays on a screen of their own (story movies, tutorials) can be skipped with Enter / A whenever.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace custom_menus::skip_movies {

// Installed on its own: nothing else depends on it. `[features] skippable_cutscenes=0` turns it off.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace custom_menus::skip_movies

// The camera's tilt follows the car's own tilt (the page's "Tilt"), instead of the game's lean with acceleration.
#pragma once

#include "nfsmw_util.h"

namespace camera_settings::tilt {

// The chase camera's placement (0x47CF00) and the call that gives its up spring the wanted up direction (0x47D270).
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace camera_settings::tilt

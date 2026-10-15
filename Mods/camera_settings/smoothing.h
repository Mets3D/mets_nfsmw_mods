// The camera's smoothing, per view (the page's "Smoothing").
#pragma once

#include "nfsmw_util.h"

namespace camera_settings::smoothing {

// The speed-keyed tweak sampler of the chase camera mover (0x46F2B0): the durations it returns are scaled.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace camera_settings::smoothing

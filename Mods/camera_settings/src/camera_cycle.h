// Next / Previous Camera View skip the views switched off on the Camera page ("Enabled").
#pragma once

#include "nfsmw_util.h"

namespace camera_settings::camera_cycle {

// PlayerSettings::ScrollDriveCam: the one step in either direction goes on until it lands on a view that is enabled.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace camera_settings::camera_cycle

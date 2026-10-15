// The camera moves with the car's acceleration (the page's "Acceleration").
#pragma once

#include "nfsmw_util.h"

namespace camera_settings::acceleration {

// CubicCameraMover::CameraAccelCurve (0x47CDD0): the offset it gives the camera for the car's acceleration is scaled.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace camera_settings::acceleration

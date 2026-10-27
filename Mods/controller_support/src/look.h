// Camera look: turning the chase cameras with the right stick, bound keys and the mouse.
#pragma once

#include "pad.h"

namespace pad::look {

void install_hooks();

// A camera of another mod looked around by the device of an extra_device slot (1 or 2): `from`, `to`, `up` are 3 floats, the matrix goes to `out`.
void look_camera(int slot, int mode, float dt, float *from, float *to, float *up, void *out);

}  // namespace pad::look

// The game's picture as a PNG file (see screenshot.cpp).
#pragma once

#include <d3d9.h>

namespace dev_tools::screenshot {

// Saves the device's back buffer, shrunk to at most `max_width` pixels wide (0 = not shrunk). Call it before Present.
bool save(IDirect3DDevice9 *device, const char *path, int max_width);

}  // namespace dev_tools::screenshot

// What other mods can draw into the players' pictures (common/split_screen_api.h): the callbacks they registered.
#pragma once

#include <d3d9.h>

namespace split_screen_support::view_api {

// Calls the registered callbacks for a view that was just drawn (see split_screen_api.h). `view_plat_info` is that view's eViewPlatInfo.
void draw(IDirect3DDevice9 *device, const void *view_plat_info, float width, float height, int player);

// Whether anyone asked to draw.
bool wanted();

}  // namespace split_screen_support::view_api

// Lines in a player's picture that show what the AI of that car is doing (see overlay.cpp).
#pragma once

#include <d3d9.h>

namespace improved_ai_opponents::overlay {

void install();  // the hook that copies the corridor the AI steers by

// Draws into the back buffer, in the middle of a player's pass (after the world is drawn, before its band is copied) with that pass's view in
// view 1's place. `view_plat_info` is that view's eViewPlatInfo. Draws nothing when that player is not AI driven.
void draw(IDirect3DDevice9 *device, const void *view_plat_info, float width, float height, int player);

}  // namespace improved_ai_opponents::overlay

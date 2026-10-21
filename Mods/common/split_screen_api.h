// What nfsmw_split_screen_support offers other mods. The functions are looked up by name at run time, because it may not be
// installed. Header only.
//
//   nfsmw_split_screen_support_on_view_draw(ViewDrawFn callback)  asks to draw into each player's picture. The callback runs after the
//                      world (and the HUD) of a view is drawn, before the picture goes to the screen: once per player in a split
//                      screen race (player 0 on top, then player 1, each with that player's view), once per frame (player 0) otherwise.
//                      The callback can draw lines and the like into the back buffer with the game's depth buffer still valid. `view_plat_info`
//                      is the eViewPlatInfo of the view being drawn (its WorldClipMatrix is at +0x80), `width` and `height` are the back
//                      buffer's. A callback is also a once-per-frame hook: it runs for player 0 on every frame the game draws.
#pragma once

#include <d3d9.h>

#include "nfsmw_util.h"

namespace nfsmw::split_screen_api {

using ViewDrawFn = void (*)(IDirect3DDevice9 *device, const void *view_plat_info, float width, float height, int player);
using OnViewDrawFn = void (*)(ViewDrawFn callback);

// Null when nfsmw_split_screen_support is not loaded.
inline OnViewDrawFn find_on_view_draw() {
    const HMODULE module = GetModuleHandleA("nfsmw_split_screen_support.asi");
    return module ? reinterpret_cast<OnViewDrawFn>(GetProcAddress(module, "nfsmw_split_screen_support_on_view_draw")) : nullptr;
}

}  // namespace nfsmw::split_screen_api

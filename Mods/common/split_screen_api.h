// What nfsmw_split_screen_support offers other mods. The functions are looked up by name at run time, because it may not be
// installed. Header only.
//
//   nfsmw_split_screen_support_on_view_draw(ViewDrawFn callback)  asks to draw into each player's picture. The callback runs after the
//                      world (and the HUD) of a view is drawn, before the picture goes to the screen: once per player in a split
//                      screen race (player 0 on top, then player 1, each with that player's view), once per frame (player 0) otherwise.
//                      The callback can draw lines and the like into the back buffer with the game's depth buffer still valid. `view_plat_info`
//                      is the eViewPlatInfo of the view being drawn (its WorldClipMatrix is at +0x80), `width` and `height` are the back
//                      buffer's. A callback is also a once-per-frame hook: it runs for player 0 on every frame the game draws.
//   int nfsmw_split_screen_support_options_player()  whose Player options (the pause menu's Options) are open: 0 to 3, the player who asked for the pause menu, or -1
//                      when that is not known (the main menu, a game of one player): the game's own PlayerToEdit (0 or 1) is then right.
//   void *nfsmw_split_screen_support_player(int index)  the game's LocalPlayer (IPlayer) of player 1 to 4 (index 0 to 3); null when there is none. Players 3 and 4
//                      are not in the game's list of local players.
//   int nfsmw_split_screen_support_wide_picture(void *camera_anchor)  1 when the picture of the player whose camera that is (a CameraAnchor, as GetPov gets it) is much
//                      wider than the screen (two players, the first of three), 0 when it has the screen's proportions (the others), -1 when that is not known.
//   nfsmw_split_screen_support_press_start(int device)  for automated tests: the Press Start screen that is showing gets "this device let go of
//                      Start" (device 0-3 = XInput controller, -1 = the keyboard), as if the person had pressed it.
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

//   nfsmw_split_screen_support_log_players()  for automated tests: every local player's car, position and render port go to the split screen log.
using LogPlayersFn = void (*)();
inline LogPlayersFn find_log_players() {
    const HMODULE module = GetModuleHandleA("nfsmw_split_screen_support.asi");
    return module ? reinterpret_cast<LogPlayersFn>(GetProcAddress(module, "nfsmw_split_screen_support_log_players")) : nullptr;
}

using OptionsPlayerFn = int (*)();
inline OptionsPlayerFn find_options_player() {
    const HMODULE module = GetModuleHandleA("nfsmw_split_screen_support.asi");
    return module ? reinterpret_cast<OptionsPlayerFn>(GetProcAddress(module, "nfsmw_split_screen_support_options_player")) : nullptr;
}

using PlayerFn = void *(*)(int index);
inline PlayerFn find_player() {
    const HMODULE module = GetModuleHandleA("nfsmw_split_screen_support.asi");
    return module ? reinterpret_cast<PlayerFn>(GetProcAddress(module, "nfsmw_split_screen_support_player")) : nullptr;
}

using WidePictureFn = int (*)(void *camera_anchor);
inline WidePictureFn find_wide_picture() {
    const HMODULE module = GetModuleHandleA("nfsmw_split_screen_support.asi");
    return module ? reinterpret_cast<WidePictureFn>(GetProcAddress(module, "nfsmw_split_screen_support_wide_picture")) : nullptr;
}

using PressStartFn = void (*)(int device);
inline PressStartFn find_press_start() {
    const HMODULE module = GetModuleHandleA("nfsmw_split_screen_support.asi");
    return module ? reinterpret_cast<PressStartFn>(GetProcAddress(module, "nfsmw_split_screen_support_press_start")) : nullptr;
}

}  // namespace nfsmw::split_screen_api

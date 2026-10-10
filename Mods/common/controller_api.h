// What nfsmw_controller_support offers other mods. The functions are looked up by name at run time, because it may not
// be installed. Header only.
//
//   nfsmw_controller_support_pads(int which)       which controllers reach the game:
//       kDefault (-1)  the mod's own setting: all XInput controllers merged into one (`merge_controllers` in its ini, on
//                      by default), or the game's own single controller when that is off
//       0..3           only that XInput controller: each XInput controller DirectInput shows the game reports that one's
//                      state, so the game's controller is that pad whichever one the game opened
//       kNoPad (-2)    none
//       kMergedPads (-3)  all XInput controllers merged into one (any button; each stick and trigger from the pad pushing
//                      it furthest)
//   nfsmw_controller_support_second_player(int slot)  a second local player (the one of the second view) has XInput
//                      controller `slot` (-1: nobody): it rumbles for their car, and their right stick looks around in
//                      their camera
//   nfsmw_controller_support_mouse_player(int player)  whose camera the mouse looks around in: 0 (the default) or 1
//   nfsmw_controller_support_keyboard(int allowed)  whether the keyboard reaches the game (1, the default) or not (0)
//   nfsmw_controller_support_keyboard_state(unsigned char *out)  the keyboard as DirectInput shows it (256 bytes, a key is
//                      down when the byte has bit 7 set), whether or not it reaches the game; 0 until the game has read it
#pragma once

#include "nfsmw_util.h"

namespace nfsmw::controller_api {

constexpr int kDefault = -1, kNoPad = -2, kMergedPads = -3;

using PadsFn = void (*)(int which);
using KeyboardFn = void (*)(int allowed);
using SecondPlayerFn = void (*)(int slot);
using MousePlayerFn = void (*)(int player);
using KeyboardStateFn = int (*)(unsigned char *out);

namespace detail {
inline void *find(const char *name) {
    const HMODULE module = GetModuleHandleA("nfsmw_controller_support.asi");
    return module ? reinterpret_cast<void *>(GetProcAddress(module, name)) : nullptr;
}
}  // namespace detail

// Each is null when nfsmw_controller_support is not loaded.
inline PadsFn find_pads() { return reinterpret_cast<PadsFn>(detail::find("nfsmw_controller_support_pads")); }
inline SecondPlayerFn find_second_player() {
    return reinterpret_cast<SecondPlayerFn>(detail::find("nfsmw_controller_support_second_player"));
}
inline MousePlayerFn find_mouse_player() {
    return reinterpret_cast<MousePlayerFn>(detail::find("nfsmw_controller_support_mouse_player"));
}
inline KeyboardFn find_keyboard() { return reinterpret_cast<KeyboardFn>(detail::find("nfsmw_controller_support_keyboard")); }
inline KeyboardStateFn find_keyboard_state() {
    return reinterpret_cast<KeyboardStateFn>(detail::find("nfsmw_controller_support_keyboard_state"));
}

}  // namespace nfsmw::controller_api

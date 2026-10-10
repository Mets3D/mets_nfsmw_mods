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
//   int nfsmw_controller_support_extra_device(int source)  a second game input device (own port) for a second local
//                      player, saying what the bindings of `source` ask for: an XInput controller 0-3, kSourceKeyboard or
//                      kSourceNone. Returns the port for the player's joystick port (FEDatabase), or -1 when there is none
//   nfsmw_controller_support_keyboard(int allowed)  whether the keyboard reaches the game (1, the default) or not (0)
//   nfsmw_controller_support_keyboard_state(unsigned char *out)  the keyboard as DirectInput shows it (256 bytes, a key is
//                      down when the byte has bit 7 set), whether or not it reaches the game; 0 until the game has read it
#pragma once

#include "nfsmw_util.h"

namespace nfsmw::controller_api {

constexpr int kDefault = -1, kNoPad = -2, kMergedPads = -3;
constexpr int kSourceKeyboard = -10, kSourceNone = -11;  // sources of the extra device (besides XInput controllers 0-3)

using PadsFn = void (*)(int which);
using KeyboardFn = void (*)(int allowed);
using ExtraDeviceFn = int (*)(int source);
using KeyboardStateFn = int (*)(unsigned char *out);

namespace detail {
inline void *find(const char *name) {
    const HMODULE module = GetModuleHandleA("nfsmw_controller_support.asi");
    return module ? reinterpret_cast<void *>(GetProcAddress(module, name)) : nullptr;
}
}  // namespace detail

// Each is null when nfsmw_controller_support is not loaded.
inline PadsFn find_pads() { return reinterpret_cast<PadsFn>(detail::find("nfsmw_controller_support_pads")); }
inline ExtraDeviceFn find_extra_device() {
    return reinterpret_cast<ExtraDeviceFn>(detail::find("nfsmw_controller_support_extra_device"));
}
inline KeyboardFn find_keyboard() { return reinterpret_cast<KeyboardFn>(detail::find("nfsmw_controller_support_keyboard")); }
inline KeyboardStateFn find_keyboard_state() {
    return reinterpret_cast<KeyboardStateFn>(detail::find("nfsmw_controller_support_keyboard_state"));
}

}  // namespace nfsmw::controller_api

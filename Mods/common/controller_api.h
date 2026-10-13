// What nfsmw_controller_support offers other mods. The functions are looked up by name at run time, because it may not
// be installed. Header only.
//
//   nfsmw_controller_support_vehicle_source(int source)  which device drives player 1's vehicle (and its camera, HUD actions
//                      and pause request): an XInput controller 0-3, kSourceKeyboard, or kDefault (-1, every device, the
//                      default). Every device still reaches the game and drives its menus.
//   int nfsmw_controller_support_extra_device(int source)  a second game input device (own port) for a second local
//                      player, saying what the bindings of `source` ask for: an XInput controller 0-3, kSourceKeyboard or
//                      kSourceNone. Returns the port for the player's joystick port (FEDatabase), or -1 when there is none
//   int nfsmw_controller_support_live_pad()  whose controller bindings the live binding table holds now (0 = the first controller's;
//                      the Controls screen can show another's). A mod keeping bindings of its own saves them only while this is 0.
//   nfsmw_controller_support_keyboard_state(unsigned char *out)  the keyboard as DirectInput shows it (256 bytes, a key is
//                      down when the byte has bit 7 set), whether or not it reaches the game; 0 until the game has read it
#pragma once

#include "nfsmw_util.h"

namespace nfsmw::controller_api {

constexpr int kDefault = -1;
constexpr int kSourceKeyboard = -10, kSourceNone = -11;  // sources of the extra device (besides XInput controllers 0-3)

using VehicleSourceFn = void (*)(int source);
using ExtraDeviceFn = int (*)(int source);
using KeyboardStateFn = int (*)(unsigned char *out);
using LivePadFn = int (*)();

namespace detail {
inline void *find(const char *name) {
    const HMODULE module = GetModuleHandleA("nfsmw_controller_support.asi");
    return module ? reinterpret_cast<void *>(GetProcAddress(module, name)) : nullptr;
}
}  // namespace detail

// Each is null when nfsmw_controller_support is not loaded.
inline VehicleSourceFn find_vehicle_source() {
    return reinterpret_cast<VehicleSourceFn>(detail::find("nfsmw_controller_support_vehicle_source"));
}
inline ExtraDeviceFn find_extra_device() {
    return reinterpret_cast<ExtraDeviceFn>(detail::find("nfsmw_controller_support_extra_device"));
}
inline LivePadFn find_live_pad() {
    return reinterpret_cast<LivePadFn>(detail::find("nfsmw_controller_support_live_pad"));
}
inline KeyboardStateFn find_keyboard_state() {
    return reinterpret_cast<KeyboardStateFn>(detail::find("nfsmw_controller_support_keyboard_state"));
}

}  // namespace nfsmw::controller_api

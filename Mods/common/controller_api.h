// What mets_controller_support offers other mods. The functions are looked up by name at run time, because it may not
// be installed. Header only.
//
//   mets_controller_support_vehicle_source(int source)  which device drives player 1's vehicle (and its camera, HUD actions
//                      and pause request): an XInput controller 0-3, kSourceKeyboard, or kDefault (-1, every device, the
//                      default). Every device still reaches the game and drives its menus.
//   int mets_controller_support_extra_device(int source)  a second game input device (own port) for a second local
//                      player, saying what the bindings of `source` ask for: an XInput controller 0-3, kSourceKeyboard or
//                      kSourceNone. Returns the port for the player's joystick port (FEDatabase), or -1 when there is none
//   int mets_controller_support_extra_device_slot(int slot, int source)  the same for more players: slot 0 is the second player's (as above), 1 and 2 are
//                      the third's and fourth's. They have no rumble or camera look.
//   int mets_controller_support_device_label(int source, char *out, int size)  the name of a device as the Controls menu shows it ("Xbox One S Controller",
//                      "Keyboard"; source: an XInput controller 0-3 or kSourceKeyboard); 0 when the controller is not known
//   mets_controller_support_look_camera(int slot, int mode, float dt, float *from, float *to, float *up, void *out)  makes the look-at matrix of a camera (eye
//                      `from`, target `to`, `up`: 3 floats each, the game's render space) with the camera look of the device of extra slot 1 or 2 (players 3 and 4) and its
//                      look-back button applied; `mode` is the camera view type (0 bumper, 1 hood, 2 and up the outside views, which can be looked around)
//   int mets_controller_support_live_pad()  whose controller bindings the live binding table holds now (0 = the first controller's;
//                      the Controls screen can show another's). A mod keeping bindings of its own saves them only while this is 0.
//   mets_controller_support_keyboard_state(unsigned char *out)  the keyboard as DirectInput shows it (256 bytes, a key is
//                      down when the byte has bit 7 set), whether or not it reaches the game; 0 until the game has read it
#pragma once

#include "nfsmw_util.h"

namespace nfsmw::controller_api {

constexpr int kDefault = -1;
constexpr int kSourceKeyboard = -10, kSourceNone = -11;  // sources of the extra device (besides XInput controllers 0-3)

using VehicleSourceFn = void (*)(int source);
using ExtraDeviceFn = int (*)(int source);
using ExtraDeviceSlotFn = int (*)(int slot, int source);
using TestInputFn = void (*)(int slot, int on, int gas, int steer);
using DeviceLabelFn = int (*)(int source, char *out, int size);
using LookCameraFn = void (*)(int slot, int mode, float dt, float *from, float *to, float *up, void *out_matrix);
using TestEntryFn = void (*)(int slot, int entry, int milli);
using TestPauseFn = void (*)(int slot, int down);
using KeyboardStateFn = int (*)(unsigned char *out);
using LivePadFn = int (*)();

namespace detail {
inline void *find(const char *name) {
    const HMODULE module = GetModuleHandleA("mets_controller_support.asi");
    return module ? reinterpret_cast<void *>(GetProcAddress(module, name)) : nullptr;
}
}  // namespace detail

// Each is null when mets_controller_support is not loaded.
inline VehicleSourceFn find_vehicle_source() {
    return reinterpret_cast<VehicleSourceFn>(detail::find("mets_controller_support_vehicle_source"));
}
inline ExtraDeviceFn find_extra_device() {
    return reinterpret_cast<ExtraDeviceFn>(detail::find("mets_controller_support_extra_device"));
}
inline ExtraDeviceSlotFn find_extra_device_slot() {
    return reinterpret_cast<ExtraDeviceSlotFn>(detail::find("mets_controller_support_extra_device_slot"));
}
inline TestInputFn find_test_input() {  // for automated tests only (dev_tools)
    return reinterpret_cast<TestInputFn>(detail::find("mets_controller_support_test_input"));
}
inline DeviceLabelFn find_device_label() {
    return reinterpret_cast<DeviceLabelFn>(detail::find("mets_controller_support_device_label"));
}
inline LookCameraFn find_look_camera() {
    return reinterpret_cast<LookCameraFn>(detail::find("mets_controller_support_look_camera"));
}
inline TestEntryFn find_test_entry() {  // for automated tests only (dev_tools)
    return reinterpret_cast<TestEntryFn>(detail::find("mets_controller_support_test_entry"));
}
inline TestPauseFn find_test_pause() {  // for automated tests only (dev_tools)
    return reinterpret_cast<TestPauseFn>(detail::find("mets_controller_support_test_pause"));
}
inline LivePadFn find_live_pad() {
    return reinterpret_cast<LivePadFn>(detail::find("mets_controller_support_live_pad"));
}
inline KeyboardStateFn find_keyboard_state() {
    return reinterpret_cast<KeyboardStateFn>(detail::find("mets_controller_support_keyboard_state"));
}

}  // namespace nfsmw::controller_api

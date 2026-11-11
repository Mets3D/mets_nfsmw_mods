// What mets_controller_support offers other mods. The functions are looked up by name at run time, because it may not
// be installed. Header only.
//
//   mets_controller_support_player_device(int player, int source, int port)  tells who plays with what (nfsmw_split_screen_support hands the devices out and
//                      makes the players' game input devices): player 0 to nfsmw::kMaxPlayers - 1, their device (an XInput controller 0-3, kSourceKeyboard,
//                      kSourceNone for anything else or nothing; player 0 also kDefault: every device, the default) and the port of their game input device
//                      in the game's input module (0 for player 0, -1 none). A player's rumble then goes to their controller, the right stick of it and the
//                      mouse of the keyboard's player look around in their own camera.
//   int mets_controller_support_actions(int xinput_slot, float *values)  what a controller asks for through its bindings of the Controls menu, per binding
//                      entry (76 floats, 0..1); 0 (nothing written) when it cannot be read
//   int mets_controller_support_device_label(int source, char *out, int size)  the name of a device as the Controls menu shows it ("Xbox One S Controller",
//                      "Keyboard"; source: an XInput controller 0-3 or kSourceKeyboard); 0 when the controller is not known
//   int mets_controller_support_live_pad()  whose controller bindings the live binding table holds now (0 = the first controller's;
//                      the Controls screen can show another's). A mod keeping bindings of its own saves them only while this is 0.
//   int mets_controller_support_second_keyboard(const SecondKeyboard *)  a second set of keys on the same keyboard, which the mod keeps and reads itself
//                      (SecondKeyboard below, which must stay valid): mets_controller_support shows it as a device of its Controls screen, where its keys
//                      are changed. Returns 0 when it cannot be shown.
#pragma once

#include "nfsmw_util.h"

namespace nfsmw::controller_api {

constexpr int kDefault = -1;
constexpr int kSourceKeyboard = -10, kSourceNone = -11;  // a player's device (besides XInput controllers 0-3)

struct SecondKeyboard {
    const char *name;                                    // the Controls screen's header
    bool (*keys)(int entry, bool defaults, int out[2]);  // the two keys of a binding entry (DirectInput codes, 0 = none), or its defaults; false: it has no keys for that entry
    void (*set_keys)(int entry, const int keys[2]);      // the player changed them on the Controls screen
};

using PlayerDeviceFn = void (*)(int player, int source, int port);
using ActionsFn = int (*)(int xinput_slot, float *values);
using DeviceLabelFn = int (*)(int source, char *out, int size);
using LivePadFn = int (*)();
using SecondKeyboardFn = int (*)(const SecondKeyboard *keyboard);

namespace detail {
inline void *find(const char *name) {
    const HMODULE module = GetModuleHandleA("mets_controller_support.asi");
    return module ? reinterpret_cast<void *>(GetProcAddress(module, name)) : nullptr;
}
}  // namespace detail

// Each is null when mets_controller_support is not loaded.
inline PlayerDeviceFn find_player_device() {
    return reinterpret_cast<PlayerDeviceFn>(detail::find("mets_controller_support_player_device"));
}
inline ActionsFn find_actions() {
    return reinterpret_cast<ActionsFn>(detail::find("mets_controller_support_actions"));
}
inline DeviceLabelFn find_device_label() {
    return reinterpret_cast<DeviceLabelFn>(detail::find("mets_controller_support_device_label"));
}
inline LivePadFn find_live_pad() {
    return reinterpret_cast<LivePadFn>(detail::find("mets_controller_support_live_pad"));
}
inline SecondKeyboardFn find_second_keyboard() {
    return reinterpret_cast<SecondKeyboardFn>(detail::find("mets_controller_support_second_keyboard"));
}

}  // namespace nfsmw::controller_api

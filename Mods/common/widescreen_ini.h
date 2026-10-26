// The widescreen fix's ini (NFSMostWanted.WidescreenFix.ini) and its ImproveGamepadSupport option, which replaces the game's
// keyboard pictures in the menus by controller pictures. Its older versions (the one in Redux v3) do it by loading a texture
// pack that redefines the pictures' names (PC_KEY_ENTER, ...), so they show controller buttons whatever the player is using,
// and the button prompts of nfsmw_controller_support, which switch between the keyboard and the controller pictures by the
// last input, have no keyboard picture left to switch to. nfsmw_controller_support does that job itself, so the option is
// turned off (see ini_switch.h). Header only, and only the file API: usable from DllMain.
#pragma once

#include "ini_switch.h"

namespace nfsmw::widescreen_ini {

using Change = ini_switch::Change;

inline bool find_path(char *out, size_t size) { return ini_switch::find_path("NFSMostWanted.WidescreenFix.ini", out, size); }

// Sets ImproveGamepadSupport to 0.
inline Change turn_off_gamepad_support(const char *path) { return ini_switch::turn_off(path, "ImproveGamepadSupport"); }

}  // namespace nfsmw::widescreen_ini

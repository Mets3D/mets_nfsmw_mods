// What nfsmw_camera_settings offers other mods. The functions are looked up by name at run time, because it may not be installed. Header only.
//
//   int nfsmw_camera_settings_mirror_wanted(int cam)  whether the rear view mirror is wanted in the camera view `cam` (PlayerSettings::CurCam): the Mirror setting of
//                      the view's preset, else the game's rule (not in the views outside the car, with the Rearview option on). 1 or 0. The split screen draws a mirror for
//                      each player with it.
#pragma once

#include "nfsmw_util.h"

namespace nfsmw::camera_settings_api {

using MirrorWantedFn = int (*)(int cam);

// Null when nfsmw_camera_settings is not loaded.
inline MirrorWantedFn find_mirror_wanted() {
    const HMODULE module = GetModuleHandleA("nfsmw_camera_settings.asi");
    return module ? reinterpret_cast<MirrorWantedFn>(GetProcAddress(module, "nfsmw_camera_settings_mirror_wanted")) : nullptr;
}

}  // namespace nfsmw::camera_settings_api

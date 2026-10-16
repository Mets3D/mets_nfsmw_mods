// "Previous Camera View": a Controls screen binding that scrolls the driving camera backwards, as the game's Change Camera View
// (renamed "Next Camera View") scrolls it forwards. Needs nothing from the other mods but the Custom Menus mod, which puts the
// row on the Controls screen; the binding itself works without it, from the ini.
#pragma once

#include "custom_menus_api.h"
#include "nfsmw_util.h"

namespace camera_settings::previous_camera {

// At start-up: takes over the entry in the game's default binding table (bound to nothing). `ini` is the mod's ini.
void init(nfsmw::Logger &log, const char *ini);
// The Controls screen row and the texts, for nfsmw_custom_menus (from the provide export).
void provide(const nfsmw::custom_menus::Host *host);
// InputPlayer::FetchInput: acts on a press of the binding.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace camera_settings::previous_camera

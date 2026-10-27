// A copy of one of the game's FEng packages under another name, so that a second HUD can show it at the same time (the HUD code finds its objects by the
// package's name, so two HUDs of one package would write into the same objects).
#pragma once

namespace split_screen_support::hud_packages {

// Makes `alias` ("HUD_PlayerB.fng") a package like `source` ("HUD_Player1.fng", which must be loaded) unless it exists already. The two names must have
// the same length. False when it could not be made (the log says why).
bool ensure_alias(const char *source, const char *alias);

}  // namespace split_screen_support::hud_packages

// What the player set per camera view that is not a number in the camera itself (main.cpp keeps the settings). `cam` is the
// game's own camera number (PlayerSettings::CurCam), which these map to a view like the Camera page does.
#pragma once

namespace camera_settings {

// Whether Next / Previous Camera View stops at the view (the page's "Enabled").
bool in_cycle(int cam);
// The page's "Mirror" for the view when it differs from the game's own (shown in the bumper and hood views only): 1 shown,
// 0 hidden, -1 the game decides.
int mirror_override(int cam);

}  // namespace camera_settings

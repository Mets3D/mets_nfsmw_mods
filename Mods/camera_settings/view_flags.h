// What the player set per camera view that is not a number in the camera itself (main.cpp keeps the settings). `cam` is the
// game's own camera number (PlayerSettings::CurCam), which these map to a view like the Camera page does.
#pragma once

namespace camera_settings {

// Smoothing of a camera view (a POV type 0-6) as a factor of the game's own (1 = unchanged).
float smoothing_factor(int type);
// How much of the game's acceleration movement of the camera a view keeps (1 = as the game has it, 0 = none).
float acceleration_factor(int type);
// How far a view's camera leans with the car's own tilt (0 = the horizon stays level, 1 = the camera's up is the car's).
float tilt_factor(int type);
// Whether Next / Previous Camera View stops at the view (the page's "Enabled").
bool in_cycle(int cam);
// The page's "Mirror" for the view when it differs from the game's own (shown in the bumper and hood views only): 1 shown,
// 0 hidden, -1 the game decides.
int mirror_override(int cam);

}  // namespace camera_settings

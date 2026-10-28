// The cameras of players 3 and 4: the game's own (see extra_cameras.cpp).
#pragma once

namespace split_screen_support::extra_cameras {

// Gets the view of the extra player (0 or 1 for players 3 and 4) ready to be drawn: its matrices and lighting made from the camera the game's mover
// placed. Returns the view (an eView), or null while the player has no camera.
void *prepare_pass(int index);

// Whether the picture of the player whose camera is `anchor` (a CameraAnchor) is much wider than the screen: 1 for the half screen bands of two players and of
// the first of three, 0 for a cell with the screen's proportions (the quarters, a grid), -1 when that is not known (not a split race, no such camera). The game's
// camera presets for split screen (GetPov) are made for the bands; nfsmw_camera_settings asks (nfsmw_split_screen_support_wide_picture).
int wide_picture(void *anchor);

void install();

}  // namespace split_screen_support::extra_cameras

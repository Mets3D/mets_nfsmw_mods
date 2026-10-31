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

// The position of the camera of an extra player (0 or 1 for players 3 and 4) in the world's (render) axes, as the game's sound code holds the cameras of players 1 and 2.
// False while the player has no camera.
bool camera_position(int extra, float *position);
// The same with the direction the camera looks in (a unit vector).
bool camera_pose(int extra, float *position, float *direction);
// The game's Camera object of an extra player (null while it has none).
void *camera_object(int extra);

void install();

}  // namespace split_screen_support::extra_cameras

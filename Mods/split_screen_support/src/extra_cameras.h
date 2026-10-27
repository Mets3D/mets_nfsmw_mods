// The cameras of players 3 and 4: the game's own (see extra_cameras.cpp).
#pragma once

namespace split_screen_support::extra_cameras {

// Gets the view of the extra player (0 or 1 for players 3 and 4) ready to be drawn: its matrices and lighting made from the camera the game's mover
// placed. Returns the view (an eView), or null while the player has no camera.
void *prepare_pass(int index);

void install();

}  // namespace split_screen_support::extra_cameras

// The rear view mirror of each player of a split screen race (see mirrors.cpp).
#pragma once

struct IDirect3DDevice9;

namespace split_screen_support::mirrors {

// The .ini's allow_rear_view_mirror: players may have mirrors (whether they do is the camera view's matter).
bool enabled();

// Once a frame, after the game moved its cameras (CameraAI::Update): the mirrors' cameras follow the players' cars.
void update(float dt);

// Around the game's drawing of the world in a player's pass: true when the drawing makes the player's mirror too, and then end_pass after it.
bool begin_pass(IDirect3DDevice9 *device, int player);
void end_pass(int player);

// After the passes: view 3 on when some mirror was drawn (the game draws the mirror's polygon only then).
void finish_frame();

// Whether the player's camera view has a mirror now (the HUD shows its frame).
bool shown(int player);

// For the mirror's polygon: false when the player has no picture this frame, else the polygon is drawn with it.
bool bind(int player);

// The device is being reset: the textures go.
void release();

void install();

}  // namespace split_screen_support::mirrors

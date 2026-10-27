// The reflections on the cars (a cube map drawn around the followed car) in a split screen race: each player's pass has its own copy of the cube map.
#pragma once

#include <d3d9.h>

namespace split_screen_support::reflections {

void install();

// A split screen race is on or over.
void set_split(bool on);

// Before the game draws the picture of `player` (0-based): that player's cube map is put in place, and for a player other than the first the game is
// told to draw it around that player's car (the first two players' cars are the ones the views follow, the others get a shift).
void begin_pass(IDirect3DDevice9 *device, int player, int player_count);

// After the picture of `player` is drawn: its cube map is kept.
void end_pass(IDirect3DDevice9 *device, int player);

// The device is going to be reset: the copies are lost.
void release();

}  // namespace split_screen_support::reflections

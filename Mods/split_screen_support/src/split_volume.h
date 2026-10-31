// A volume for each player of a split screen race (Options > Player > Split-Screen Volume; see split_volume.cpp).
#pragma once

namespace split_screen_support::split_volume {

constexpr int kStep = 10;  // percent; the volume is 0 to 100

int percent(int player);
void set_percent(int player, int percent);  // takes effect at once, is not saved
void save(int player);
// A positioned sound (the output block of its SFXCTL_3DObjPos) is worked out for this player's listener this frame.
void note_sound(const void *block, int player);

// The gain noted for a 3D control's output block this round (1 when the sound is not scaled).
float gain_of(const void *block);

void install();

}  // namespace split_screen_support::split_volume

// The track streamer for four players (see extra_streaming.cpp).
#pragma once

namespace split_screen_support::extra_streaming {

// The extra places (the cars that have none of the streamer's two): their player (from 0), zone and the place (0 or 1) whose bit their sections get. Returns how many.
int extra_places(int *players, int *zones, int *slots, int max);

void install();

}  // namespace split_screen_support::extra_streaming

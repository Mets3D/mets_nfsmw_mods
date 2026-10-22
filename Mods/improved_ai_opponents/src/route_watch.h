// Says in the log when a car's route is dropped, and why it looks like (see route_watch.cpp).
#pragma once

namespace improved_ai_opponents::route_watch {

// Once per frame for a player's car with an AI (or planning beside it): its drive-to nav (a WRoadNav) and the car's position (x, y, z).
void frame(int player, const void *nav, const float *car);
// This player's route was dropped by us on purpose (F9): the next drop is that one.
void requested(int player);

}  // namespace improved_ai_opponents::route_watch

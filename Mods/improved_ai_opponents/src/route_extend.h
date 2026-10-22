// Keeps an improved AI's route from running out (see route_extend.cpp).
#pragma once

namespace improved_ai_opponents::route_extend {

// Once per frame for a player's car whose AI is the improved one: its drive-to nav (a WRoadNav) and the car's position (x, y, z).
void frame(int player, void *nav, const float *car);

}  // namespace improved_ai_opponents::route_extend

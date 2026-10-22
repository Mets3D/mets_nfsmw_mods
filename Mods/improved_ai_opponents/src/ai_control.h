// The players' cars driven by the game's AI (the "Drunk Driver" of the Extra Options mod, applied to each player), either the game's own
// ("vanilla") or one with our changes ("improved"), to compare them side by side. Who drives and how is in settings.h.
#pragma once

namespace improved_ai_opponents::control {

void install();

// Once per frame (player 0's draw): the AI control, the hotkeys, the cars' AIs the rest of the mod asks for.
void frame();

// The player (0 or 1) whose car's drive-to nav `nav` (a WRoadNav) is, when that car's AI is the improved one (also while a human drives it and
// the improved AI only plans beside the car). -1 for any other nav.
int improved_player(const void *nav);

// The velocity (x, y, z) of that player's car; false when it has no body (yet).
bool car_velocity(int player, float velocity[3]);

// What an IVehicleAI (see vehicle_ai) gives: its drive-to nav (a WRoadNav) and its car's position (x, y, z), null when it has none.
void *drive_to_nav(void *vehicle_ai);
const float *car_position(void *vehicle_ai);

// The player (0 or 1) whose car, when that car's AI is the improved one (as above), is within `radius` metres of `point` (x, y, z), -1 if none.
int improved_player_near(const float point[3], float radius);

// Player `player`'s IVehicleAI while there is an AI to draw for that player (the AI drives the car or plans beside it, and the overlay is on for the
// player), else null.
void *driving_ai(int player);

}  // namespace improved_ai_opponents::control

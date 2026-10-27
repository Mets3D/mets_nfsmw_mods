// The HUD of each player in a race with more than two players: each player's HUD package is drawn into that player's cell of the screen.
#pragma once

namespace split_screen_support::hud_views {

// The HUD package `name` ("HUD_Player1.fng") belongs to `player` (0-based) of `players`; while it is drawn the screen is that player's cell.
void assign(const char *package, int player, int players);

// Forget all assignments (the race is over).
void clear();

void install();

}  // namespace split_screen_support::hud_views

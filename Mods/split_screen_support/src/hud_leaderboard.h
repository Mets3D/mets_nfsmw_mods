// The leaderboard of a split screen HUD, whose package has none: a copy of the single player race HUD's leaderboard (see hud_leaderboard.cpp).
#pragma once

namespace split_screen_support::hud_leaderboard {

// After a split screen HUD is made and its objects classified: `player` (0-based), `number` the player's settings index.
void attach(void *hud, const char *package, int player, int number);

// Before a HUD goes.
void detach(void *hud);

}  // namespace split_screen_support::hud_leaderboard

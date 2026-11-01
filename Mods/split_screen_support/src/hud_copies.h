// What the split screen HUD packages lack and the single player race HUD has: the leaderboard and the mirror's housing, copied into them (see hud_copies.cpp).
#pragma once

namespace split_screen_support::hud_copies {

// After a split screen HUD is made and its objects classified: `player` (0-based), `number` the player's settings index. `leaderboard`: the HUD shows the leaderboard
// (not a drag race's).
void attach(void *hud, const char *package, int player, int number, bool leaderboard);

// Every frame (also while the pause menu is up): a copy of the mirror's housing is shown while its player has a mirror.
void install();

// Before a HUD goes.
void detach(void *hud);

}  // namespace split_screen_support::hud_copies

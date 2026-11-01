// What the split screen HUD packages lack and the single player race HUD has: the leaderboard and the mirror's housing, copied into them (see hud_copies.cpp).
#pragma once

namespace split_screen_support::hud_copies {

// After a split screen HUD is made and its objects classified: `player` (0-based), `number` the player's settings index.
void attach(void *hud, const char *package, int player, int number);

// Every frame before the game updates a HUD: the mirror's housing is shown while the player has a mirror.
void update(void *hud, int player);

// Before a HUD goes.
void detach(void *hud);

}  // namespace split_screen_support::hud_copies

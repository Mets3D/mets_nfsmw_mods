// The HUD of each player in a race with more than two players: each player's HUD package is drawn into that player's cell of the screen, each corner of
// the HUD anchored to the corner of the cell and scaled by the player's HUD scale.
#pragma once

namespace split_screen_support::hud_views {

// The HUD package `name` ("HUD_Player1.fng") belongs to `player` (0-based) of `players`; while it is drawn the screen is that player's cell.
void assign(const char *package, int player, int players);

// Works out which corner of the screen each object of a HUD package belongs to (from where the HUD's layout puts it), so each can be anchored to its own
// corner of the cell. Call once the HUD is made (and its minimap placed).
void classify(const char *package);

// An object that was made later (a copy of a minimap marker) belongs where `model` does.
void classify_like(void *object, void *model);

// A player's HUD scale in percent (30 to 100, 100 = the HUD fills its cell): 4 players only; a corner keeps its place in the cell.
void set_scale(int player, int percent);
int scale(int player);

// Forget all assignments (the race is over).
void clear();

void install();

}  // namespace split_screen_support::hud_views

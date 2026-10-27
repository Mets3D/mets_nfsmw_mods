// Player 2's own minimap, a colour for each player's marker.
#pragma once

namespace split_screen_support::minimap {

// Moves the map picture, ring and backdrop of a minimap by `dy` FE units (negative: up) in the HUD package `package`, and keeps them there (the layout's
// scripts put them back). The markers are placed by the minimap around its map centre, which moves with them.
void place(void *minimap, const char *package, float dy);

// The markers of a minimap that is not one of the two of a game for two players (hud_players.cpp makes them): its own marker has the colour `own_rgb` and
// the second marker follows `other_player` (an IPlayer) in `other_rgb`. Forgotten when the HUDs go.
void set_markers(void *minimap, unsigned own_rgb, void *other_player, unsigned other_rgb);

void install();

}  // namespace split_screen_support::minimap

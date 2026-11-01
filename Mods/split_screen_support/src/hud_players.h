// The HUDs of a race with more than two players: one for each player, each in that player's cell of the screen (see hud_players.cpp).
#pragma once

namespace split_screen_support::hud_players {

// The player (0-based) whose HUD `hud` is in a race with more than two players, -1 when it is not one of those.
int player_of_hud(void *hud);

// Which player asked for the pause menu last (0-based): the game only knows the settings index of the player (0 or 1), which the players 3 and 4 share
// with 1 and 2. -1 when it is not known (use the settings index).
int pause_requester();

// The colour (0xRRGGBB) of a player's marker on the minimaps (and the world map), 0-based.
unsigned player_colour(int player);

void install();

}  // namespace split_screen_support::hud_players

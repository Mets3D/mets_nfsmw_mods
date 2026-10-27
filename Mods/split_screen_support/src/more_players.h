// Players 3 and 4: a second round of Press Start and Car Select (see more_players.cpp).
#pragma once

namespace split_screen_support::more_players {

// A new race setup (the race options screen was built): the next race starts with the first round.
void reset();

// 0 in the first round (players 1 and 2), 1 in the second (players 3 and 4).
int round();

// A Press Start screen was made (`screen` is the MenuScreen): in the second round its text says player 3 or 4.
void on_press_start_screen(void *screen);

// Logs every local player: their car, position and render port (for automated tests).
void log_players();

// Players 3 and 4 of a race with four players: how many there are now (0 or 2), and a chase camera for one of them (0 or 1): eye, target and up
// vectors in the game's render space (4 floats each). False when they have no car.
int extra_player_count();

// The race that is about to load or is running has four players (the Players row says 4 and the second round of picks is there).
bool four_player_race();

// The car of player 1 to 4 (index 0 to 3) in simulation space: position and forward vector (3 floats each). False when there is no such car.
bool player_car_state(int index, float *position, float *forward);
bool extra_camera(int index, float *eye, float *target, float *up);

// The device that pressed Start for player 1 to 4 (0-3 a controller, players::kKeyboard) in the picks of the two rounds; -2 when the player did not
// choose (or has not yet). A device used in the first round can not be chosen in the second.
int pick_device(int player);
bool device_used_in_first_round(int device);

// The LocalPlayer of an extra player (0 or 1 for players 3 and 4), null when there is none.
void *extra_player(int index);

// While the game updates the HUD of an extra player the player has a render port that is a view (the HUD code asks that view about the camera), and
// otherwise none. `player` that is not an extra player is left alone.
void lend_view(void *player, bool on);

void install();

}  // namespace split_screen_support::more_players

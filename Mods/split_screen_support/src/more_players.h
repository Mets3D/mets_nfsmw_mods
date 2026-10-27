// Players 3 and 4: a second round of Press Start and Car Select (see more_players.cpp), with the first player only for three players.
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

// Players 3 and 4 of a race with three or four players: how many there are now (0 to 2).
int extra_player_count();

// How many players the race that is about to load or is running has: 1 when it is not a split screen race.
int race_players();

// How many players beyond the first two the race that is about to load or is running has: 0, 1 or 2 (the Players row says 3 or 4 and the second round of picks is
// there).
int extras_in_race();

// The car of player 1 to 4 (index 0 to 3) in simulation space: position and forward vector (3 floats each). False when there is no such car.
bool player_car_state(int index, float *position, float *forward);

// The device that pressed Start for player 1 to 4 (0-3 a controller, players::kKeyboard) in the picks of the two rounds; -2 when the player did not
// choose (or has not yet). A device used in the first round can not be chosen in the second.
int pick_device(int player);
bool device_used_in_first_round(int device);

// The LocalPlayer of an extra player (0 or 1 for players 3 and 4), null when there is none.
void *extra_player(int index);

// The PlayerSettings (0x14 bytes) of an extra player: a copy of player 1's or 2's made with the race. Null when there is none.
unsigned char *extra_settings(int index);

// While the game updates the HUD of an extra player the player has a render port that is a view (the HUD code asks that view about the camera), and
// otherwise none. `player` that is not an extra player is left alone.
void lend_view(void *player, bool on);

// Whether the extra player (0 or 1 for players 3 and 4) is in a running race and has a car.
bool extra_ready(int index);

// The game finds the player a camera director is for among the local players by their render port, and the extra players are not in that list: for the
// duration of the game's own setup of their camera (on = true until it is called again with false) the extra player is the only local player, in the view
// the camera is made for.
void make_only_local_player(int index, bool on);

// The Car Select screens of a split screen race say which player and which device (all split screen races, not only those with more than two players).
void install_car_select_label();

void install();

}  // namespace split_screen_support::more_players

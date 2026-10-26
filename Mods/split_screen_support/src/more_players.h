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

void install();

}  // namespace split_screen_support::more_players

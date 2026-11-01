// Where the cars of players 3 and up start (see start_grid.cpp).
#pragma once

namespace split_screen_support::start_grid {

// The intro cutscene of the race has just ended (race_intro.cpp): the first four racers stand where it left them. Their places and the shape of the grid are noted, and are
// what the others are placed by.
void cutscene_over();

// The Start row of the split screen race options: who stands in front. For the races that start after this.
enum class Start { kPlayersFront, kPlayersRear, kRandom };
void set_start(Start start);

void install();

}  // namespace split_screen_support::start_grid

// The Quick Race's opponents, difficulty, catch up and heat rows in a split screen race (see ai_in_split_screen.cpp).
#pragma once

#include "custom_menus_api.h"

namespace split_screen_support::ai_in_split_screen {

// The Players row changed: the total of racers is never below the players.
void players_changed();

// Registers the Total Racers row texts (from the nfsmw_custom_menus provide export); without them the Opponents row is added instead.
void register_texts(const nfsmw::custom_menus::Host *host);

// Called by Game_InitRacers' hook before and after the game makes the race's racers (see ai_in_split_screen.cpp: the race context and the number of opponents).
void prepare_race();
void racers_made();

void install();

}  // namespace split_screen_support::ai_in_split_screen

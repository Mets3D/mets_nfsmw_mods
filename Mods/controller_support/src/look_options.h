// Options > Player > Free Look Reset: how long a player's free look (look.cpp) waits before the view returns behind the car, or never.
#pragma once

#include "custom_menus_api.h"

namespace pad::look_options {

// Reads the settings: each player's delay from [look_player_1] / [look_player_2] return_delay, else [look] return_delay.
void load();

// Seconds without look input before the view of player `who` (0 or 1) returns; 0 = never.
float return_delay(int who);

// The row in Options > Player (through nfsmw_custom_menus).
void provide(const nfsmw::custom_menus::Host *host);

}  // namespace pad::look_options

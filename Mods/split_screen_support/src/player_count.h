// The "Players" row of the split screen race options (Track_Options.fng): how many people race (see player_count.cpp).
#pragma once

#include "custom_menus_api.h"

namespace split_screen_support::player_count {

// How many players the next split screen race has: 2 or 4.
int wanted();

// Registers the row's texts (from the nfsmw_custom_menus provide export); the row only exists when they could be registered.
void register_texts(const nfsmw::custom_menus::Host *host);

void install();

}  // namespace split_screen_support::player_count

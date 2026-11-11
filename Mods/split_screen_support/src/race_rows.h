// The Layout, Start Lineup and (tollbooth) Time rows of the split screen race options (see race_rows.cpp).
#pragma once

#include "custom_menus_api.h"

namespace split_screen_support::race_rows {

// Registers the rows' texts (from the nfsmw_custom_menus provide export); the rows only exist when they could be registered.
void register_texts(const nfsmw::custom_menus::Host *host);

// After player_count::install (the rows come under the Players row) and before the AI rows.
void install();

}  // namespace split_screen_support::race_rows

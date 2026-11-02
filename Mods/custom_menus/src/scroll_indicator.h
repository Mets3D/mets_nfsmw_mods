// A scroll bar for a screen whose layout has none (Track_Options.fng, the race options), for a list with more rows than the screen shows: a track and a handle next to the rows.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace custom_menus::scroll_indicator {

// The list of `screen` (a UIWidgetMenu) is longer than the rows it shows, and its view is at the top: makes the bar. Nothing for a screen with no such list.
void add(void *screen, nfsmw::Logger &log);

// Every frame: the handle at the place of the rows in view.
void update(void *screen);

// The screen is left (its package and the bar with it).
void forget();

}  // namespace custom_menus::scroll_indicator

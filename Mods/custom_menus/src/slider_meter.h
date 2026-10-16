// A thin live bar under a slider row (Page::live in custom_menus_api.h): where the slider shows a setting, this shows what the thing
// the setting is for is doing now (how far a stick is pushed, against a dead zone slider).
#pragma once

#include "nfsmw_util.h"

namespace custom_menus::slider_meter {

void init(nfsmw::Logger *log);  // where the geometry of the first meters is logged, to tune their place

// Shows the meter of the slider in slot `slot` (1-based, the n of the layout's BASE_SLIDER_n / FILLBAR_SLIDER_n) of the screen whose
// package is `package`, filled to `level` of the slider's length (0..1); a negative `level` hides it. The meter is made the first time
// it is wanted (a copy of the slider's fill bar, kept in the package), so call this every tick.
void set(const char *package, int slot, float level);

}  // namespace custom_menus::slider_meter

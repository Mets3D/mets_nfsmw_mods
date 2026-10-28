// Changes to the game's code that let a split screen race load and show up. Only the scenery detail one has a switch in the ini ([options]).
#pragma once

namespace split_screen_support::fixes {

void install();

// Every object that implements IVehicleCache (the vehicles, QuickGame...) is in a global UTL::Vector at 0x92FDA0 with room for 16 in the exe's data. A race of
// four players has more than 16 at once: the vector then takes a bigger buffer from the race's memory, which is freed when the race ends while the vector keeps
// pointing at it, and the next race of four writes into it (access violation in the Listable<IVehicleCache> constructor). This moves the list to a buffer of ours
// with room for far more than any race has, before the race starts.
void vehicle_cache_room();

// The scenery detail rule (see scenery_detail in fixes.cpp): the one view rule (objects over 17 pixels, the model by size) or the game own rule for
// two views (over 22 pixels, the lowest detail). Only does anything when scenery_detail was installed. Four players use the game own: each of their
// views is drawn full size and shown at a quarter of the area.
void use_one_view_scenery_rule(bool one_view);

}  // namespace split_screen_support::fixes

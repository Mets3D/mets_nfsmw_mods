// Changes to the game's code that let a split screen race load and show up. Only the scenery detail one has a switch in the ini ([options]).
#pragma once

namespace split_screen_support::fixes {

void install();

// Two global UTL::Vectors of the game have an inline buffer and an allocator that never allocates more: the HUDs (Listable<IHud>, room for 2, at 0x92FD88) and
// the objects that implement IVehicleCache (the vehicles, QuickGame..., room for 28, at 0x92FDA0). The buffer of the HUDs ends where the vehicle caches' vector begins:
// the third and fourth HUD of a four player race overwrote its allocator and its buffer pointer, and the next race crashed in the Listable<IVehicleCache>
// constructor (or in the HUD code, when the vehicle caches were many). This gives both a buffer of ours with room for far more than any race has, before the
// race starts.
void list_room();

// The scenery detail rule (see scenery_detail in fixes.cpp): the one view rule (objects over 17 pixels, the model by size) or the game own rule for
// two views (over 22 pixels, the lowest detail). Only does anything when scenery_detail was installed. Four players use the game own: each of their
// views is drawn full size and shown at a quarter of the area.
void use_one_view_scenery_rule(bool one_view);

}  // namespace split_screen_support::fixes

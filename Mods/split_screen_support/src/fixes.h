// Changes to the game's code that let a split screen race load and show up. Only the scenery detail one has a switch in the ini ([options]).
#pragma once

namespace split_screen_support::fixes {

void install();

// The scenery detail rule (see scenery_detail in fixes.cpp): the one view rule (objects over 17 pixels, the model by size) or the game own rule for
// two views (over 22 pixels, the lowest detail). Only does anything when scenery_detail was installed. Four players use the game own: each of their
// views is drawn full size and shown at a quarter of the area.
void use_one_view_scenery_rule(bool one_view);

}  // namespace split_screen_support::fixes

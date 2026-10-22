// Inspecting the road network from inside the game (agent_docs/ai-driving.md): shows the road nodes and segments around the camera, picks the one the
// camera looks at, labels it and says what is stored in it in the log (see road_inspector.cpp).
#pragma once

#include "lines.h"

namespace improved_ai_opponents::road_inspector {

// The player's overlay mode (settings::Overlay), told for every picture: the selection starts again when it changes.
void set_mode(int player, int mode);
// Adds the nodes and segments near the camera to the batch, the picked ones highlighted: for the Roads mode (settings::kOverlayRoads) the segments of the
// active node, for the Cost mode (settings::kOverlayCost) the route picked so far and the cost of it, for the car of `player`.
void add(canvas::Lines &lines, int player, int mode);
// Starts a picture: the labels placed so far are forgotten (so that none of the picture's labels covers another).
void begin_picture();

}  // namespace improved_ai_opponents::road_inspector

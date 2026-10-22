// Inspecting the road network from inside the game (agent_docs/ai-driving.md): shows the road nodes and segments around the camera, picks the one the
// camera looks at, labels it and says what is stored in it in the log (see road_inspector.cpp).
#pragma once

#include "lines.h"

namespace improved_ai_opponents::road_inspector {

void install();  // reads [road_inspector] in the ini
bool enabled();  // the key toggles it (polled here: called once per player's picture, the key is seen once per press)
// Adds the nodes and segments near the camera, the picked ones highlighted, to the batch.
void add(canvas::Lines &lines);
// Starts a picture: the labels placed so far are forgotten (so that none of the picture's labels covers another).
void begin_picture();

}  // namespace improved_ai_opponents::road_inspector

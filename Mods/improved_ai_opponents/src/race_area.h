// Which road segments and nodes are part of the race that is on, and which lie outside it (see race_area.cpp).
#pragma once

namespace improved_ai_opponents::race_area {

// A race is on and has a route (some segments are flagged in race): only then are there segments outside the race. When it is false everything counts as inside.
bool active();

// The segment / node is inside the race's area: it can be reached from the race's route without crossing a track barrier.
bool contains_segment(unsigned segment);
bool contains_node(unsigned node);

// Which way the race goes along a segment, as a unit vector (x, z): its own direction for a segment of the route (flag 2: along node 0 to node 1), else the
// direction of the route's segment nearest to it (within 60 m). False when the race has no direction there. Roads beside the route share its direction; a
// road that crosses it does not matter, since it is only asked whether a segment runs against the race.
bool route_direction(unsigned segment, float *x, float *z);

}  // namespace improved_ai_opponents::race_area

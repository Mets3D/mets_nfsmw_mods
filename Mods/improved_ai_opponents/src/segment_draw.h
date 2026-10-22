// A road segment drawn into a picture (see segment_draw.cpp).
#pragma once

#include "lines.h"

namespace improved_ai_opponents::segment_draw {

// One road segment as a corridor (its left and right edge over the drivable width, curved where the road is), in `edge`; with a `fill` colour (alpha counts) it is
// filled in strips of two shades, with a bar across it at every step. Only the part from `t_from` to `t_to` (the curve's parameter, 0 at the first node) is drawn.
void add_corridor(canvas::Lines &lines, unsigned segment_index, DWORD edge, DWORD fill = 0, float t_from = 0.0f, float t_to = 1.0f);

// One road segment as the lanes of its profile: strips coloured by lane type (2 yellow: rough, 7 and 13 red: lanes into obstacles, the others green: the road).
void add_lanes(canvas::Lines &lines, unsigned segment_index, float t_from = 0.0f, float t_to = 1.0f);

}  // namespace improved_ai_opponents::segment_draw

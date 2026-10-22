// The smackables (the props a car can knock down: signposts, barrels, lamp posts) as obstacles for the improved AI's steering (see obstacles.cpp).
#pragma once

namespace improved_ai_opponents::canvas {
class Lines;
}

namespace improved_ai_opponents::obstacles {

void install();

// Draws the collision boxes of the props within 80 m of the car (x, y, z) of `player`, with their mass, light ones too, and which the AI steers around (see obstacles.cpp).
// `nav` is the car's drive-to nav (null: not known).
void add_boxes(canvas::Lines &lines, const float *car, int player, const void *nav);

}  // namespace improved_ai_opponents::obstacles

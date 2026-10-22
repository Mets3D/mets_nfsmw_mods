// The smackables (the props a car can knock down: signposts, barrels, lamp posts) as obstacles for the improved AI's steering (see obstacles.cpp).
#pragma once

namespace improved_ai_opponents::canvas {
class Lines;
}

namespace improved_ai_opponents::obstacles {

void install();

// Draws the collision boxes of the props within 80 m of the car (x, y, z), with their mass, light ones too (see obstacles.cpp).
void add_boxes(canvas::Lines &lines, const float *car);

}  // namespace improved_ai_opponents::obstacles

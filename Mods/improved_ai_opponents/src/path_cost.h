// The route search of the improved AI weighs the roads (see path_cost.cpp).
#pragma once

namespace improved_ai_opponents::path_cost {

void install();

// What the route search of the improved AI costs one road segment, for a car that enters it at `entry_speed` (m/s): its length, the speed a car can follow it at,
// the speed the car leaves it at, the seconds it takes and the cost in metres (seconds times the top speed), for the car of `player` (-1: the first player's that is
// known). Without the extras of a real search (the wrong-way term): the time itself.
struct StepCost {
    float length, limit, entry, exit, seconds, cost;
};
StepCost cost_of_step(unsigned segment, float entry_speed, int player = -1);

}  // namespace improved_ai_opponents::path_cost

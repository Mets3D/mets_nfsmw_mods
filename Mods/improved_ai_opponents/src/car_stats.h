// What a player's car can do, for the improved AI's route search (see car_stats.cpp).
#pragma once

namespace improved_ai_opponents::car_stats {

struct Stats {
    float top_speed;  // m/s
    float lateral;    // m/s^2: the sideways acceleration the tires hold
    float brake;      // m/s^2
    float grip;       // the tires' static grip in g (0: not known)
    float accel[10];  // m/s^2 along the car's speed from 0 to top_speed in nine equal steps
    bool from_car;    // the numbers are the car's own (else those of a generic car)
};

// Every frame of a race: the numbers of the car of a player whose IVehicleAI this is (null: none), from the game: its top speed and acceleration (the AI's own tables,
// IVehicleAI::GetTopSpeed / GetAcceleration) and its tires' grip.
void refresh(int player, void *vehicle_ai);
// The tires attributes of the player's car (SuspensionRacer + 0xA0, seen by the steering hook).
void note_tires(int player, const void *tires);

// The numbers of a player's car; -1: of the first one that has a car.
const Stats &of(int player);

float acceleration(const Stats &stats, float speed);       // m/s^2 at that speed
float cornering_speed(const Stats &stats, float radius);  // the speed at which the tires hold a bend of that radius (at most the top speed)

}  // namespace improved_ai_opponents::car_stats

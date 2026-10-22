// What a player's car can do, as the game knows it:
//  - top speed and acceleration: the AI's own numbers (AIVehicle::mTopSpeed and the table mAccelData that Physics::Info::ComputeAccelerationTable makes from the car's
//    attributes when the vehicle is made; for every car, a human's too) through IVehicleAI::GetTopSpeed (vtable slot 29) and GetAcceleration(speed) (slot 30);
//  - grip: the static grip of the tires (Attrib tires STATIC_GRIP, front and rear, in g; the rear one or the front one, the lower: what AIActionRace::GetPotentialSpeed
//    takes as its StartGrip), read from the tires attributes that the suspension holds (SuspensionRacer + 0xA0; the steering hook in ai_control.cpp passes it on).
//    The downforce that adds grip at speed (AIActionRace's EndGrip) is not in it: a little less grip than the car has at speed.
//  - braking: the game's AI has no braking model (it brakes fully or not at all), so a car is taken to slow down as hard as its tires hold: the same as it corners.
// Where a number cannot be had (no race, a car the game's classes do not fit) a generic car is used instead (kDefault...).
#include "car_stats.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <windows.h>

#include "improved_ai_opponents.h"

namespace improved_ai_opponents::car_stats {

namespace {

constexpr int kSlotTopSpeed = 29, kSlotAcceleration = 30;  // IVehicleAI's virtual functions
constexpr unsigned kStaticGrip = 0x38;                     // Attrib tires layout: STATIC_GRIP {front, rear}, in g
constexpr float kGravity = 9.8f;                           // what the game's AI uses

constexpr float kDefaultTopSpeed = 70.0f, kDefaultLateral = 10.0f, kDefaultBrake = 12.0f, kDefaultAccel = 8.0f;  // m/s, m/s^2: a generic car

void fill_default_table(Stats *s, float accel_from_standing) {
    for (int i = 0; i < 10; ++i) s->accel[i] = std::max(0.0f, accel_from_standing * (1.0f - static_cast<float>(i) / 9.0f));
}

Stats make_default() {
    Stats s{};
    s.top_speed = kDefaultTopSpeed;
    s.lateral = kDefaultLateral;
    s.brake = kDefaultBrake;
    fill_default_table(&s, kDefaultAccel);
    return s;
}

const Stats g_default = make_default();
Stats g_stats[2] = {g_default, g_default};
const void *g_tires[2] = {nullptr, nullptr};
DWORD g_last_refresh[2] = {0, 0};

bool sane(float v, float low, float high) { return std::isfinite(v) && v >= low && v <= high; }

}  // namespace

void note_tires(int player, const void *tires) {
    if (player >= 0 && player < 2) g_tires[player] = tires;
}

void refresh(int player, void *vehicle_ai) {
    if (player < 0 || player > 1) return;
    if (!vehicle_ai) {
        g_stats[player] = g_default;
        g_tires[player] = nullptr;
        return;
    }
    const DWORD now = GetTickCount();
    if (now - g_last_refresh[player] < 250) return;
    g_last_refresh[player] = now;

    Stats s = g_default;
    void **vtable = *static_cast<void ***>(vehicle_ai);
    const float top = reinterpret_cast<float(__thiscall *)(void *)>(vtable[kSlotTopSpeed])(vehicle_ai);
    if (sane(top, 10.0f, 200.0f)) {
        s.top_speed = top;
        bool table_ok = true;
        for (int i = 0; i < 10; ++i) {
            s.accel[i] = reinterpret_cast<float(__thiscall *)(void *, float)>(vtable[kSlotAcceleration])(vehicle_ai, top * static_cast<float>(i) / 9.0f);
            table_ok = table_ok && sane(s.accel[i], -50.0f, 80.0f);
        }
        if (!table_ok) {
            s.top_speed = g_default.top_speed;
            memcpy(s.accel, g_default.accel, sizeof(s.accel));
        } else {
            s.from_car = true;
        }
    }
    const float *tires = static_cast<const float *>(g_tires[player]);
    if (tires && !IsBadReadPtr(tires, kStaticGrip + 8)) {
        const float grip = std::min(tires[kStaticGrip / 4], tires[kStaticGrip / 4 + 1]);
        if (sane(grip, 0.3f, 4.0f)) {
            s.lateral = grip * kGravity;
            s.brake = grip * kGravity;
        }
    }
    const Stats &old = g_stats[player];
    if (std::fabs(old.top_speed - s.top_speed) > 0.01f || std::fabs(old.lateral - s.lateral) > 0.01f || old.from_car != s.from_car || std::fabs(old.accel[0] - s.accel[0]) > 0.01f)
        g_log.line("car stats: player %d's %s: top speed %.1f m/s, accelerates %.1f m/s^2 from standing and %.1f at half the top speed, tires hold %.1f m/s^2 (brakes %.1f)", player + 1,
                   s.from_car ? "car" : "car (not known: the ini's numbers)", s.top_speed, s.accel[0], s.accel[5], s.lateral, s.brake);
    g_stats[player] = s;
}

const Stats &of(int player) {
    if (player >= 0 && player < 2) return g_stats[player];
    for (int p = 0; p < 2; ++p)
        if (g_stats[p].from_car) return g_stats[p];
    return g_default;
}

float acceleration(const Stats &stats, float speed) {
    const float at = std::clamp(speed / std::max(1.0f, stats.top_speed), 0.0f, 1.0f) * 9.0f;
    const int i = std::min(8, static_cast<int>(at));
    const float a = stats.accel[i] + (stats.accel[i + 1] - stats.accel[i]) * (at - static_cast<float>(i));
    return std::max(0.3f, a);
}

float cornering_speed(const Stats &stats, float radius) { return std::min(stats.top_speed, std::sqrt(stats.lateral * radius)); }

}  // namespace improved_ai_opponents::car_stats

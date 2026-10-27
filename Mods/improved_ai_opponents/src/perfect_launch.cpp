// A perfect launch is a boost at the start of a race: PVehicle::UpdateStaging (sub_6702D0, every frame while the car is staging) works out how far the engine's revs are into
// a window, `(rpm - start) / range`, and when the car is released (IVehicle::Launch 0x6700E0) a bonus of (1 + that) x a constant above zero is kept for 4.5 s: it forces the
// engine to its peak torque and raises the traction (EngineRacer, SuspensionRacer). The window is IRaceEngine::GetPerfectLaunchRange: the revs that give the peak torque
// (or 500 below the redline, whichever is lower) down to a quarter of the span from idle to the redline below them, first gear only. Above the window there is nothing, so
// the higher in it the better, but not so high that a bit of overshoot ends it.
// The game's AI holds a constant throttle of 0.8 while it stages (AIVehiclePid::OnGasBrake): the revs end up wherever that puts them. So the improved AI holds the revs a
// share of the way up the window with a controller on the throttle: proportional to how far the revs are from there, with an integral for the throttle that holds them and
// a damping term on how fast they rise.
#include "perfect_launch.h"

#include <windows.h>

#include <algorithm>

#include "improved_ai_opponents.h"

namespace improved_ai_opponents::perfect_launch {

namespace {

constexpr float kTargetShare = 0.7f;   // of the window, from its start
constexpr float kP = 2.0f, kI = 0.8f, kD = 0.1f;  // per window's width of error; per second; seconds (on the window widths per second the revs rise)
constexpr float kIntegralStart = 0.3f, kWindupLimit = 0.6f;  // the integral starts here, and only gathers when the error is under this (in windows)
constexpr DWORD kNewStaging = 500;  // ms without a call: a new staging
constexpr DWORD kSayEvery = 300;    // ms between lines of the debug log

struct State {
    DWORD last = 0, said = 0;
    float integral = kIntegralStart, rpm = 0.0f;
    bool have_rpm = false;
};
State g_state[kPlayers];

}  // namespace

float throttle(int player, float rpm, float start, float range, float dt, float fallback) {
    const int p = slot_of(player);
    State &s = g_state[p];
    const DWORD now = GetTickCount();
    if (now - s.last > kNewStaging) s = State();
    s.last = now;
    if (!(range > 1.0f && start > 0.0f)) return fallback;
    const float target = start + kTargetShare * range;
    const float error = (target - rpm) / range;
    const float rate = s.have_rpm && dt > 1e-4f ? (rpm - s.rpm) / dt / range : 0.0f;
    s.rpm = rpm;
    s.have_rpm = true;
    if (std::fabs(error) < kWindupLimit) s.integral = std::clamp(s.integral + kI * error * dt, 0.0f, 1.0f);
    const float gas = std::clamp(kP * error + s.integral - kD * rate, 0.0f, 1.0f);
    if (g_debug_log && now - s.said > kSayEvery) {
        s.said = now;
        g_log.line("perfect launch: player %d's revs are %.0f, the window is %.0f to %.0f, the throttle %.2f", player + 1, rpm, start, start + range, gas);
    }
    return gas;
}

}  // namespace improved_ai_opponents::perfect_launch

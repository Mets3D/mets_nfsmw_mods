// The game's AI burns nitrous by the state of the car, not the road: it starts when the speed is 15 m/s or more under what the corridor allows and the car accelerates
// at under half of what it can, ends after 4 s or when the bottle is empty, and starts again at 30% of a bottle (agent_docs/ai-driving.md, "Nitrous"). A bottle is
// only 2.5 to 5 s of burn and takes 30 to 50 s to fill again (more slowly at lower speeds), so that makes short bursts at random places.
// The improved AI's rule uses the route it has planned: a burn is for a stretch where the car can accelerate. How much it wants one depends on how full the bottle is (a
// full bottle stops recharging, so a nearly full one is burnt wherever the road is open) and on how much speed the car has to gain (out of a corner, a part bottle is
// burnt too). It ends when a bend is near, a car is in the way, or the car is at its top speed. That also puts a burn at the start of a race: the bottle is full and the
// road is open from the first moment (the engine itself engages nitrous from 10 mph). Two things come before all that: the finish line (the whole bottle is burnt so that it
// is empty at the line: unused nitrous is worth nothing after it) and a speed trap ahead in a speed trap race (the game's own check for one, which its rule uses too). A burn is a commitment: any burn stops the bottle recharging for a while (the engine's
// NOS_DISENGAGE, 2 s), so none is started for less than about a second of road, and one that has begun lasts at least kMinBurn whatever happens (only an empty bottle, or the
// rule of the game that follows, ends it earlier). The rest of the game's rule stays: no nitrous while staging, turning round or stopping, and the nitrous that the weakest
// human player has (Fair Nitrous).
#include "nitro.h"

#include <windows.h>

#include <algorithm>

#include "ai_control.h"
#include "car_stats.h"
#include "improved_ai_opponents.h"
#include "path_cost.h"
#include "settings.h"

namespace improved_ai_opponents::nitro {

namespace {

// AIActionRace (PC offsets) and its Update's locals at the point where the decision is made (see ai_control.cpp, patch_ai).
constexpr unsigned kActionEngine = 0x4C, kActionTurnAround = 0x67, kActionBottleTime = 0x94;
constexpr unsigned kFrameNeedToStop = 0x12, kFrameStaging = 0x13, kFrameSpeed = 0x14, kFrameWasNos = 0x28, kFrameNav = 0x2C, kFramePotentialNos = 0x30, kFrameDt = 0x88;
constexpr unsigned kEngineNosCapacity = 0x1C;  // IEngine vtable
constexpr unsigned kNavType = 0x78, kNavSegment = 0x8E, kNavPathCount = 0x2E0, kNavPath = 0x2E4, kNavHasTrail = 0x55, kNavAvoidableOcclusion = 0x5C;
constexpr int kPathType = 3, kMaxPath = 500;
constexpr uintptr_t kRaceStatus = 0x91E000, kCheckSpeedTraps = 0x414B30;  // GRaceStatus::fObj; AIActionRace::CheckSpeedTraps(speed, skill, potential_nos, was_nos)
constexpr unsigned kStatusRaceLength = 0x45D4, kInfoPctRaceComplete = 0x18, kInfoFinished = 0x30;  // GRaceStatus, GRacerInfo

constexpr float kMinSpeed = 10.0f;     // m/s: the speed the planning assumes at least
constexpr float kMargin = 5.0f;        // m/s: a segment limits the car if its speed limit is not this much over the car's speed
constexpr float kRunCap = 600.0f;      // m of route looked at
constexpr float kMinBottle = 0.5f;     // below this no burn is started
constexpr float kFull = 0.99f;         // a bottle this full is wasting its recharge
constexpr float kFarRun = 3.0f, kNearRun = 0.3f, kFullRun = 0.15f;  // the open road needed, in burns' lengths: with a half bottle, a full one, one that is wasting
constexpr float kSlowRatio = 0.8f, kFastRatio = 0.97f;              // the speed, in top speeds, that a burn is started under with a half bottle and with a full one
constexpr float kExitRatio = 0.65f;    // under this share of the top speed a burn is worth more (out of a corner), more so the lower the speed
constexpr float kStopRatio = 0.98f;    // a burn ends at this share of the top speed
constexpr float kBendSeconds = 0.7f;   // a burn ends when the open road ahead is under this many seconds
constexpr float kStartRoadSeconds = 1.2f;  // a burn starts when there is at least this many seconds of open road ahead (more than kBendSeconds, or it would end at once)
constexpr float kMinBurn = 0.6f;       // s: a burn that has started lasts this long at least (unless the game's rule or an empty bottle ends it)
constexpr float kRestartGap = 1.0f;    // s: after a burn another starts only when the bottle is wasting its recharge, or after this long
constexpr float kDefaultBottleSeconds = 2.5f;
constexpr float kFinishFactor = 1.5f;  // the finish burn starts when the line is this many times the distance the bottle covers, at the speed the car does, away
constexpr float kMinFinishBottle = 0.05f;

template <typename T> T read(const void *base, unsigned offset) { return *reinterpret_cast<const T *>(static_cast<const unsigned char *>(base) + offset); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

// How far along the planned route (metres, from the start of the segment the nav is on) the car can go without a segment that limits it to less than `speed` plus a
// margin; -1 when there is no route to look at.
float clear_run(const void *nav, float speed, int player) {
    if (!nav || IsBadReadPtr(nav, 0x300) || read<int>(nav, kNavType) != kPathType) return -1.0f;
    const int count = read<int>(nav, kNavPathCount);
    const unsigned short *path = read<const unsigned short *>(nav, kNavPath);
    if (count <= 0 || count > kMaxPath || IsBadReadPtr(path, count * 2)) return -1.0f;
    const unsigned current = read<unsigned short>(nav, kNavSegment);
    int first = -1;
    for (int i = 0; i < count && first < 0; ++i)
        if (path[i] == current) first = i;
    if (first < 0) return -1.0f;
    float run = 0.0f;
    for (int i = first; i < count && run < kRunCap; ++i) {
        const path_cost::StepCost step = path_cost::cost_of_step(path[i], speed, player);
        if (step.limit < speed + kMargin) break;
        run += step.length;
    }
    return run;
}

DWORD g_waiting_said[kPlayers] = {}, g_progress_said[kPlayers] = {};
float g_burn_seconds[kPlayers] = {}, g_rest_seconds[kPlayers] = {1e9f, 1e9f, 1e9f, 1e9f};  // how long the car has been burning, and not burning

// How far the race has left to go for a player's car (metres), -1 when that is not known: the part of the race the car has done (GRacerInfo::mPctRaceComplete, as a share or
// as a percentage) of the race's length (GRaceStatus::fRaceLength). Both are checked for being plausible, since the layouts are from the decomp.
float race_remaining(int player, float *progress) {
    const unsigned char *info = static_cast<const unsigned char *>(control::racer_info(player));
    const unsigned char *status = *reinterpret_cast<const unsigned char *const *>(kRaceStatus);
    if (!info || !status || IsBadReadPtr(info, 0x40) || IsBadReadPtr(status + kStatusRaceLength, 4)) return -1.0f;
    if (read<unsigned char>(info, kInfoFinished)) return -1.0f;
    const float done = read<float>(info, kInfoPctRaceComplete), length = read<float>(status, kStatusRaceLength);
    *progress = done;
    if (!(length > 200.0f && length < 300000.0f) || !(done >= 0.0f && done <= 100.5f)) return -1.0f;
    return std::max(length * (1.0f - (done > 1.0f ? done * 0.01f : done)), 0.0f);
}

}  // namespace

bool decide(int player, const unsigned char *action, const unsigned char *frame, bool game_wants) {
    const int p = slot_of(player);
    const bool was_on = read<unsigned char>(frame, kFrameWasNos) != 0;
    const float potential_nos = read<float>(frame, kFramePotentialNos);
    const bool can = !read<unsigned char>(action, kActionTurnAround) && !read<unsigned char>(frame, kFrameNeedToStop) && !read<unsigned char>(frame, kFrameStaging) && potential_nos > 0.0f;
    const float speed = read<float>(frame, kFrameSpeed);
    const float dt = read<float>(frame, kFrameDt);
    g_burn_seconds[p] = was_on ? g_burn_seconds[p] + dt : 0.0f;
    g_rest_seconds[p] = was_on ? 0.0f : g_rest_seconds[p] + dt;
    if (!can) {
        if (was_on && g_debug_log) g_log.line("smarter nitro: player %d's nitrous stops at %.0f m/s (the bottle is empty, or the game's rule: staging, turning round, a speed over its limit)", player + 1, speed);
        return false;
    }
    void *engine = read<void *>(action, kActionEngine);
    const void *nav = read<const void *>(frame, kFrameNav);
    if (!engine || !nav) return game_wants;
    const float bottle = reinterpret_cast<float(__thiscall *)(void *)>((*static_cast<void ***>(engine))[kEngineNosCapacity / 4])(engine);
    const float planning_speed = std::max(speed, kMinSpeed);
    const float top_speed = car_stats::of(p).top_speed;
    const float ratio = top_speed > 0.0f ? speed / top_speed : 0.5f;  // how much of its top speed the car does
    const bool car_in_the_way = read<unsigned char>(nav, kNavHasTrail) && read<int>(nav, kNavAvoidableOcclusion) != 0;
    const float bottle_seconds = read<float>(action, kActionBottleTime) > 0.0f ? read<float>(action, kActionBottleTime) : kDefaultBottleSeconds;
    const float burn_distance = 1.3f * planning_speed * bottle_seconds * bottle;  // what the car covers while it burns what the bottle has (it speeds up)
    const auto conclude = [&](bool on, const char *why, float run) {
        if (on != was_on && g_debug_log)
            g_log.line("smarter nitro: player %d %s at %.0f m/s (%.0f%% of its top speed), %.0f%% of a bottle, %.0f m of open road ahead (%s)", player + 1, on ? "starts" : "stops", speed,
                       ratio * 100.0f, bottle * 100.0f, run, why);
        return on;
    };

    // The finish line, and a speed trap: before the road, which they do not depend on.
    float progress = 0.0f;
    const float remaining = race_remaining(player, &progress);
    if (g_debug_log) {
        const DWORD now = GetTickCount();
        if (now - g_progress_said[p] > 5000) {
            g_progress_said[p] = now;
            g_log.line("smarter nitro: player %d has done %.3f of the race (the race info says), %.0f m left, %.0f%% of a bottle", player + 1, progress, remaining, bottle * 100.0f);
        }
    }
    if (bottle > kMinFinishBottle && remaining >= 0.0f && remaining <= kFinishFactor * planning_speed * bottle_seconds * bottle) return conclude(true, "the finish line", remaining);
    if (bottle > kMinFinishBottle && reinterpret_cast<bool(__thiscall *)(const void *, float, float, float, bool)>(kCheckSpeedTraps)(action, speed, settings::player(player).skill / 100.0f, potential_nos, was_on))
        return conclude(true, "a speed trap", -1.0f);

    const float run = clear_run(nav, planning_speed, p);
    if (run < 0.0f) return game_wants;  // no route to go by: the game's rule

    bool on;
    const char *why = "";
    if (was_on) {
        const bool to_stop = car_in_the_way || ratio >= kStopRatio || run < kBendSeconds * planning_speed;
        on = !to_stop || g_burn_seconds[p] < kMinBurn;
        why = car_in_the_way ? "a car is in the way" : ratio >= kStopRatio ? "the top speed" : "a bend is near";
    } else {
        const float fullness = clamp01((bottle - kMinBottle) / (1.0f - kMinBottle));  // 0 at half a bottle, 1 at a full one
        const float exit = clamp01((kExitRatio - ratio) / kExitRatio);                  // out of a corner: a burn is worth more the slower the car
        const float want = std::max(fullness, exit);
        const float needed = (bottle >= kFull ? kFullRun : lerp(kFarRun, kNearRun, want)) * burn_distance;
        const float fast_enough = lerp(kSlowRatio, kFastRatio, fullness);
        const bool rested = bottle >= kFull || g_rest_seconds[p] >= kRestartGap;
        on = bottle >= kMinBottle && !car_in_the_way && ratio <= fast_enough && run >= std::max(needed, kStartRoadSeconds * planning_speed) && rested;
        why = bottle >= kFull ? "a full bottle" : exit > fullness ? "out of a corner" : "open road";
        if (!on && bottle >= kFull && g_debug_log) {
            const DWORD now = GetTickCount();
            if (now - g_waiting_said[p] > 3000) {
                g_waiting_said[p] = now;
                g_log.line("smarter nitro: player %d waits with a full bottle at %.0f m/s (%.0f%% of its top speed): %.0f m of open road, %.0f m needed, a car in the way: %s", player + 1, speed, ratio * 100.0f,
                           run, needed, car_in_the_way ? "yes" : "no");
            }
        }
    }
    return conclude(on, why, run);
}

}  // namespace improved_ai_opponents::nitro

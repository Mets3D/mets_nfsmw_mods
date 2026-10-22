// The players' cars driven by the game's racing AI: what ExtraOptions' F6 does to the one player in a single player game, for either player.
// Game_ForceAIControl 0x6123B0 (cdecl, 0 = first local player, anything else = the last) finds the player's IHumanAI and calls
// SetAiControl(true) (AIVehicleHuman::SetAiControl 0x4291C0: goal "AIGoalRacer", AI physics on); Game_ClearAIControl 0x612420 gives the car back.
// Each player has settings of their own (settings.h: the ini and the Options > AI Driving page): who drives (a human, the game's AI "vanilla",
// or the AI with our changes "improved", below, which apply to the cars set to improved only, so that the two can be compared), the AI's skill,
// nitrous and performance (fair or not), shortcuts, the race route, steering (super steer) and catch-up. A human driven car whose overlay is AI Logic has the improved AI plan its route beside it ("observe"), so that the
// overlay can show what the AI would do. The settings take effect at once: every frame of a race compares them with what was applied.
// F7 hands player 1's car to the improved AI and back, F9 makes the AIs plan their route again.
// What the AI is: the game's racing AI is made for the opponents (a DRIVER_RACER car, AIPerpVehicle's skill and catch-up). A player's car under AI
// control has a few differences (agent_docs/ai-driving.md), which are taken away here for both the vanilla and the improved AI, so that "vanilla"
// is how the AI races as an opponent: its nav is of the racer path type (the game's own rule for shortcuts), it is let use nitrous (the opponents'
// rule), its skill is the player's skill setting (AIVehicleHuman::GetSkill is a constant 1.0; here the base class's own, fed by the setting) and
// the catch-up setting runs the game's own catch-up code (AIPerpVehicle::Update skips it for a player's car).
#include "ai_control.h"

#include <iterator>

#include "car_stats.h"
#include "hooks.h"
#include "improved_ai_opponents.h"
#include "keys.h"
#include "nitro.h"
#include "patch.h"
#include "road_geometry.h"
#include "route_extend.h"
#include "route_watch.h"
#include "settings.h"

namespace improved_ai_opponents::control {

namespace {

enum class Variant { kOff, kVanilla, kImproved };

constexpr uintptr_t kGameFlowState = 0x925E90;  // 6 = in the race
constexpr uintptr_t kPlayerList = 0x92D87C, kPlayerCount = 0x92D884;  // the local players
constexpr uintptr_t kForceAIControl = 0x6123B0, kClearAIControl = 0x612420, kCancelPathFinding = 0x770060, kFind = 0x5D59F0;
constexpr uintptr_t kRaceStatus = 0x91E000, kGetRacerInfo = 0x5E8B20;  // GRaceStatus::fObj; GRaceStatus::GetRacerInfo(ISimable *)
constexpr unsigned kIHumanAIHandle = 0x403770, kIVehicleAIHandle = 0x403700;  // IHumanAI::_IHandle, IVehicleAI::_IHandle
constexpr int kFramesBeforeStart = 120;
constexpr int kHumanAIOffset = 8;  // the IHumanAI of an AIVehicleHuman is 8 bytes into the object (the game's own thunk subtracts 8)
// AIPerpVehicle, seen from the IVehicleAI pointer (the object is 0x4C before it): the base skill and the glue skill (GetSkill 0x409340 adds them), the racer info
// (IPerpetrator is 0x758 further on, SetRacerInfo stores it at +0x4C); and the nav's path type (WRoadNav +0x7C: 2 racer, 4 player).
constexpr int kBaseSkill = 0x75C, kGlueSkill = 0x760, kRacerInfo = 0x758, kNavPathType = 0x7C, kPerpVehicleToVehicleAi = 0x4C;
constexpr uintptr_t kPerpGetSkill = 0x409340, kPerpGetCatchupCheat = 0x409390;

using ForceFn = void(__cdecl *)(int);
using GetFn = void *(__thiscall *)(void *);
using FindFn = void *(__thiscall *)(void *, void *);
using GetAiControlFn = bool(__thiscall *)(void *);

int g_frames = 0;
int g_tries[2] = {0, 0};
bool g_done[2] = {false, false};
bool g_replan[2] = {false, false};  // make the car's AI plan its route again (the control changed, F9)
int g_applied_control[2] = {-1, -1};  // the control setting last acted on (-1: none yet this race)
bool g_path_type_set[2] = {false, false};  // the car's nav was given the racer path type
void *g_vehicle_ai[2] = {nullptr, nullptr};  // each player's IVehicleAI (what the game's AI code passes around), refreshed every frame
void *g_human_ai[2] = {nullptr, nullptr};    // each player's IHumanAI

Variant variant_of(int player) {
    const settings::Player &s = settings::player(player);
    if (s.control == settings::kImproved) return Variant::kImproved;
    if (s.control == settings::kVanilla) return Variant::kVanilla;
    return s.overlay == settings::kOverlayAi ? Variant::kImproved : Variant::kOff;  // a human drives: the improved AI only plans beside the car, for the overlay
}
bool wants_control(int player) { return settings::player(player).control != settings::kOff; }

// A local player's interface (player 0 is the first in the list, 1 the last, as Game_ForceAIControl picks them), null when there is none yet.
void *interface_of_player(int player, unsigned handle) {
    const int count = *reinterpret_cast<int *>(kPlayerCount);
    if (count < 1 || (player == 1 && count < 2)) return nullptr;
    void **list = *reinterpret_cast<void ***>(kPlayerList);
    void *p = list[player == 0 ? 0 : count - 1];
    if (!p) return nullptr;
    void *simable = reinterpret_cast<GetFn>((*static_cast<void ***>(p))[1])(p);
    if (!simable) return nullptr;
    return reinterpret_cast<FindFn>(kFind)(*reinterpret_cast<void **>(static_cast<char *>(simable) + 4), reinterpret_cast<void *>(handle));
}

bool ai_controlled(void *human_ai) { return reinterpret_cast<GetAiControlFn>((*static_cast<void ***>(human_ai))[3])(human_ai); }

// The player whose car's AI this is (an IVehicleAI), -1 for any other car (the opponents).
int player_of(const void *vehicle_ai) {
    for (int player = 0; player < 2; ++player)
        if (vehicle_ai && g_vehicle_ai[player] == vehicle_ai) return player;
    return -1;
}

// The same for the `this` of AIVehicleHuman's own methods (GetSkill...): the object itself or its IVehicleAI.
int player_of_self(const void *self) {
    for (int player = 0; player < 2; ++player) {
        if (!self) break;
        if (g_vehicle_ai[player] == self || (g_human_ai[player] && static_cast<const char *>(g_human_ai[player]) - kHumanAIOffset == self)) return player;
    }
    return -1;
}

// The ISimable of an IVehicleAI, and its IRigidBody.
void *simable_of(void *vehicle_ai) { return vehicle_ai ? reinterpret_cast<GetFn>((*static_cast<void ***>(vehicle_ai))[1])(vehicle_ai) : nullptr; }
void *body_of(void *vehicle_ai) {
    void *simable = simable_of(vehicle_ai);
    return simable ? reinterpret_cast<GetFn>((*static_cast<void ***>(simable))[0x54 / 4])(simable) : nullptr;
}

// The game's racer info of a player's car (what an opponent's AI keeps as pRacerInfo), null when the game has none.
void *racer_info_of(int player) {
    void *simable = simable_of(g_vehicle_ai[player]);
    void *status = *reinterpret_cast<void **>(kRaceStatus);
    return simable && status ? reinterpret_cast<void *(__thiscall *)(void *, void *)>(kGetRacerInfo)(status, simable) : nullptr;
}

// What makes a player's car under AI control an opponent's racing AI: the skill from the setting, the racer info (the catch-up code and the race
// filter of the nav want it), and the nav's path type. Every frame: the game sets some of them again when the car is reset.
void feed_racer(int player) {
    char *ai = static_cast<char *>(g_vehicle_ai[player]);
    if (!ai) return;
    const settings::Player &s = settings::player(player);
    void *nav = drive_to_nav(g_vehicle_ai[player]);
    int *path_type = nav ? reinterpret_cast<int *>(static_cast<char *>(nav) + kNavPathType) : nullptr;
    if (s.control == settings::kOff || !g_done[player]) {
        if (path_type && g_path_type_set[player]) {
            *path_type = kPathPlayer;
            g_path_type_set[player] = false;
        }
        return;
    }
    *reinterpret_cast<float *>(ai + kBaseSkill) = static_cast<float>(s.skill) / 100.0f;
    void **racer_info = reinterpret_cast<void **>(ai + kRacerInfo);
    if (!*racer_info) {
        *racer_info = racer_info_of(player);
        static int logged = 0;
        if (logged < 4) g_log.line("ai control: player %d's AI had no racer info, now %p", player + 1, *racer_info), ++logged;
    }
    if (path_type) {
        const int want = s.control == settings::kVanilla ? kPathRacer : kPathPlayer;  // vanilla: the game's own shortcut rule for a racer
        if (*path_type != want && (want == kPathRacer || g_path_type_set[player])) {
            *path_type = want;
            g_path_type_set[player] = want == kPathRacer;
        }
    }
}

// A line with both cars' positions every 2 s, with the time of day, to match what the player says about where they are ("now": the line nearest in time).
void log_positions() {
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (now - last < 2000) return;
    last = now;
    float p[2][3] = {};
    bool have[2] = {false, false};
    for (int player = 0; player < 2; ++player) {
        const float *v = car_position(g_vehicle_ai[player]);
        if (v) {
            memcpy(p[player], v, 12);
            have[player] = true;
        }
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    if (have[0] && have[1])
        g_log.line("cars %02d:%02d:%02d: player 1 at %.0f %.0f %.0f, player 2 at %.0f %.0f %.0f", t.wHour, t.wMinute, t.wSecond, p[0][0], p[0][1], p[0][2], p[1][0], p[1][1], p[1][2]);
}

void on_world_load() {
    g_frames = 0;
    g_tries[0] = g_tries[1] = 0;
    g_done[0] = g_done[1] = false;
    g_applied_control[0] = g_applied_control[1] = -1;
}

// Acts on a change of a player's control setting (the page, F7): hands the car to the AI, or back.
void apply_control(int player) {
    const int control = settings::player(player).control;
    if (g_applied_control[player] == control) return;
    const bool first = g_applied_control[player] < 0;
    g_applied_control[player] = control;
    if (control == settings::kOff) {
        if (!first && ai_controlled(g_human_ai[player])) {
            reinterpret_cast<ForceFn>(kClearAIControl)(player);
            g_log.line("ai control: player %d drives again", player + 1);
        }
        g_done[player] = true;
        return;
    }
    g_done[player] = false;
    g_tries[player] = 0;
    if (!first) {
        g_frames = kFramesBeforeStart;  // now, not in 2 s
        g_replan[player] = true;
        g_log.line("ai control: player %d's car is handed to the %s AI", player + 1, control == settings::kImproved ? "improved" : "vanilla");
    }
}

void on_frame() {
    const int state = *reinterpret_cast<int *>(kGameFlowState);
    static int last_state = 0;
    if (state == 6 && last_state != 6) on_world_load();  // a race has begun
    last_state = state;
    const int players = *reinterpret_cast<int *>(kPlayerCount);  // 1 in a race with one local player, 2 in a split screen race
    const bool racing = state == 6 && players >= 1;
    g_vehicle_ai[0] = g_vehicle_ai[1] = nullptr;
    g_human_ai[0] = g_human_ai[1] = nullptr;
    if (!racing) {
        g_frames = 0;
        for (int player = 0; player < 2; ++player) car_stats::refresh(player, nullptr);
        return;
    }
    for (int player = 0; player < 2; ++player) {
        g_vehicle_ai[player] = interface_of_player(player, kIVehicleAIHandle);
        g_human_ai[player] = interface_of_player(player, kIHumanAIHandle);
    }
    log_positions();
    for (int player = 0; player < 2; ++player) car_stats::refresh(player, g_vehicle_ai[player]);
    // The corner limits of the roads are worked out ahead in the background (the first route search asks for thousands), as soon as an improved AI is there to ask
    if (variant_of(0) == Variant::kImproved || variant_of(1) == Variant::kImproved) geometry::precompute_limits();
    // F7 hands player 1's car to the improved AI and back (to try the AI from wherever the car is), like ExtraOptions' F6 does for the one player.
    // It changes the setting for this run only; the page's Accept saves it.
    static bool f7_was_down = false;
    if (nfsmw::key_pressed(nfsmw::key_down(VK_F7), f7_was_down)) settings::player(0).control = settings::player(0).control == settings::kOff ? settings::kImproved : settings::kOff;
    // F9: the AIs plan their route again, from where they are. The game plans it once and then follows it (AIActionRace asks for a new path
    // only while the drive-to nav is not on one); WRoadNav::CancelPathFinding drops the path, so the next frame asks again.
    static bool f9_was_down = false;
    if (nfsmw::key_pressed(nfsmw::key_down(VK_F9), f9_was_down)) g_replan[0] = g_replan[1] = true;
    for (int player = 0; player < 2; ++player)
        if (g_human_ai[player]) apply_control(player);
    for (int player = 0; player < 2; ++player) feed_racer(player);
    for (int player = 0; player < 2; ++player) {
        if (!g_replan[player] || !g_vehicle_ai[player] || variant_of(player) == Variant::kOff) continue;
        void *nav = drive_to_nav(g_vehicle_ai[player]);
        if (nav) reinterpret_cast<void(__thiscall *)(void *)>(kCancelPathFinding)(nav);
        g_replan[player] = false;
        route_watch::requested(player);
        g_log.line("ai control: player %d plans its route again", player + 1);
    }
    for (int player = 0; player < 2; ++player) {
        if (!g_vehicle_ai[player] || variant_of(player) == Variant::kOff) continue;
        void *nav = drive_to_nav(g_vehicle_ai[player]);
        const float *car = car_position(g_vehicle_ai[player]);
        route_watch::frame(player, nav, car);
        if (settings::player(player).control == settings::kImproved) route_extend::frame(player, nav, car);
    }
    const auto waiting = [&](int player) { return player < players && wants_control(player) && !g_done[player]; };
    const bool pending = waiting(0) || waiting(1);
    if (!pending) return;
    if (++g_frames < kFramesBeforeStart) return;
    g_frames = 0;
    for (int player = 0; player < 2; ++player) {
        if (!waiting(player)) continue;  // no such player in this race (player 2 when the game is not split), or nothing to do
        void *ai = g_human_ai[player];
        if (!ai) {
            if (++g_tries[player] <= 5) g_log.line("ai control: player %d has no IHumanAI yet (try %d)", player + 1, g_tries[player]);
            continue;
        }
        if (!ai_controlled(ai)) reinterpret_cast<ForceFn>(kForceAIControl)(player);
        g_log.line("ai control: player %d AI control %s (%s AI)", player + 1, ai_controlled(ai) ? "ON" : "still off",
                   variant_of(player) == Variant::kImproved ? "improved" : "vanilla");
        if (ai_controlled(ai) || ++g_tries[player] > 5) g_done[player] = true;
    }
}

// ---- our changes to the racing AI (agent_docs/ai-driving.md). The stubs ask which player's AI it is with the action's IVehicleAI (+0x40 of an
// AIAction) and otherwise do what the game's code did.

//  - Nitrous: AIActionRace::Update skips it when the owner is a player (the `jne` after the IsPlayer call at 0x42905E): the AI of a player's car is let
//    through. Then the game's own rule is "fair": AIActionRace scales the nitrous it may use to what the weakest human player has (mNosCapability +0x8C and
//    mUsableNOS +0x90 of the action, set when it starts), so with no nitrous for the humans there is none for the AI. With Fair Nitrous off for an improved AI
//    that is lifted: GetPotentialNOS 0x407BE0 is called with the two fields raised.
unsigned __cdecl nitrous_allowed(const void *vehicle_ai) {
    const int player = player_of(vehicle_ai);
    return player >= 0 && wants_control(player) ? 1u : 0u;
}

//  - Smarter nitro: where AIActionRace::Update hands its decision to the car's input (0x4291A0: `mov eax, [esp+24h]` = want_nos; `mov edx, [ecx]` before
//    `push eax; call [edx+20h]`, IInput::SetControlNOS), nitro.cpp may decide instead. It gets the action and the address of Update's frame: the locals the decision
//    is made from are read from it (need_to_stop 0x12, is_staging 0x13, actual speed 0x14, was_nos 0x28, the nav 0x2C, potential_nos 0x30).
unsigned __cdecl nitro_decision(const unsigned char *action, const unsigned char *frame) {
    const bool game_wants = *reinterpret_cast<const unsigned *>(frame + 0x24) != 0;
    const int player = player_of(*reinterpret_cast<void *const *>(action + 0x40));
    if (player < 0 || variant_of(player) != Variant::kImproved || !settings::player(player).smarter_nitro) return game_wants ? 1u : 0u;
    return nitro::decide(player, action, frame, game_wants) ? 1u : 0u;
}
char g_nitro_fpu_state[112];  // fnsave / frstor around the call: the game has the x87 stack in use

using PotentialNosFn = float(__fastcall *)(void *, void *, float, bool, float);
PotentialNosFn g_orig_potential_nos = nullptr;

float __fastcall hook_potential_nos(void *self, void *edx, float speed, bool was_on, float skill) {
    constexpr unsigned kSpeedLimit = 0x58;    // the action's fSpeedLimit
    constexpr float kMinSpeedForNos = 10.0f;  // the game's own (fMinSpeedForNOS)
    const int player = player_of(*reinterpret_cast<void **>(static_cast<char *>(self) + 0x40));
    const bool improved = player >= 0 && variant_of(player) == Variant::kImproved;
    // Smarter Nitro (nitro.cpp) decides by the road, the bottle, the finish and the speed traps, so the game's own reasons to refuse nitrous are lifted: the car does less than
    // 10 m/s (the engine itself only engages it from 10 mph), the car does the speed that the AI's own model says it should (fSpeedLimit, which ended a burn for a frame at a
    // time while the car outspeeded the model), and the bottle must be over 30% to start (as if the nitrous were on already, only what Fair Nitrous keeps back is).
    const bool smarter = improved && settings::player(player).smarter_nitro;
    float *speed_limit = reinterpret_cast<float *>(static_cast<char *>(self) + kSpeedLimit);
    const float old_speed_limit = *speed_limit;
    if (smarter) {
        *speed_limit = 1.0e9f;
        speed = std::max(speed, kMinSpeedForNos);
        was_on = true;
    }
    float result;
    if (!improved || settings::player(player).fair_nitrous) {
        result = g_orig_potential_nos(self, edx, speed, was_on, skill);
    } else {
        float *capability = reinterpret_cast<float *>(static_cast<char *>(self) + 0x8C), *usable = reinterpret_cast<float *>(static_cast<char *>(self) + 0x90);
        const float old_capability = *capability, old_usable = *usable;
        if (*capability <= 0.0f) *capability = 1.0f;
        *usable = 1.0f;
        result = g_orig_potential_nos(self, edx, speed, was_on, skill);
        *capability = old_capability;
        *usable = old_usable;
    }
    if (smarter) *speed_limit = old_speed_limit;
    return result;
}

//  - Fair performance: AIActionRace::ComputePotentials 0x428450 sets what the AI believes its car can do: the top speed (mTopSpeed +0xA0) and the grip (mStartGrip
//    +0x98, mEndGrip +0x9C) are the lowest of those of all the player cars and its own, because the performance bias (+0x80..) is 0 outside a career race; in a
//    career race the bias leads from that towards its own car by how well the player's car is rated. With Fair Performance off for an improved AI the numbers are
//    made its own car's after the game has worked them out: the top speed it has, the static grip of its tires, and the end grip by the same ratio to the
//    static grip that the game found.
using ComputePotentialsFn = void(__fastcall *)(void *, void *);
ComputePotentialsFn g_orig_compute_potentials = nullptr;

void __fastcall hook_compute_potentials(void *self, void *edx) {
    constexpr unsigned kVehicleAi = 0x40, kStartGrip = 0x98, kEndGrip = 0x9C, kTopSpeed = 0xA0;
    g_orig_compute_potentials(self, edx);
    char *action = static_cast<char *>(self);
    const int player = player_of(*reinterpret_cast<void **>(action + kVehicleAi));
    if (player < 0 || variant_of(player) != Variant::kImproved || settings::player(player).fair_performance) return;
    const car_stats::Stats &car = car_stats::of(player);
    float &start_grip = *reinterpret_cast<float *>(action + kStartGrip), &end_grip = *reinterpret_cast<float *>(action + kEndGrip), &top_speed = *reinterpret_cast<float *>(action + kTopSpeed);
    g_log.line("ai control: player %d's AI holds back to the weakest player's car no more: top speed %.1f -> %.1f m/s, grip %.2f -> %.2f g", player + 1, top_speed, car.from_car ? car.top_speed : top_speed,
               start_grip, car.grip > 0.0f ? car.grip : start_grip);
    if (car.from_car) top_speed = car.top_speed;
    if (car.grip > 0.0f) {
        end_grip = start_grip > 0.0f ? end_grip / start_grip * car.grip : car.grip;
        start_grip = car.grip;
    }
}

//  - Off path: AIActionRace::CheckOffPath 0x414780 (a task every 0.25 s) puts the drive-to nav back on the road nearest to the car (AIVehicle::ResetDriveToNav, which
//    drops the route) when the nav is out of bounds (more than 2 m) and the road the car is on is neither in the nav's cookie trail nor in its route. The nav is 30
//    to 100 m ahead of the car and a route begins at the nav, so the road the car is on is behind the route's start: on a corner cut the nav is on the cut while the car
//    is still on the road before it, and the game threw the route away and planned from the car's road (the long way round) every time. For an improved AI that
//    is skipped while the car is within kLostDistance of its nav (further, the car is lost: the game's own rule).
constexpr float kLostDistance = 150.0f;
using CheckOffPathFn = void(__fastcall *)(void *, void *, float);
CheckOffPathFn g_orig_check_off_path = nullptr;

void __fastcall hook_check_off_path(void *self, void *edx, float dt) {
    constexpr unsigned kVehicleAi = 0x40, kNavPosition = 0x98;
    const int player = player_of(*reinterpret_cast<void **>(static_cast<char *>(self) + kVehicleAi));
    if (player >= 0 && variant_of(player) == Variant::kImproved) {
        const unsigned char *nav = static_cast<const unsigned char *>(drive_to_nav(g_vehicle_ai[player]));
        const float *car = car_position(g_vehicle_ai[player]);
        if (nav && car && !IsBadReadPtr(nav, kNavPosition + 12)) {
            const float *at = reinterpret_cast<const float *>(nav + kNavPosition);
            if (std::hypot(at[0] - car[0], at[2] - car[2]) < kLostDistance) return;
        }
    }
    g_orig_check_off_path(self, edx, dt);
}

//  - Super steer: the physics steers a car with SuspensionRacer::DoSteering, by DoHumanSteering (the wheel's range falls with speed, throttle and brake, and it turns
//    at a limited rate) when IHumanAI::IsPlayerSteering (AIVehicleHuman, 0x435240: `!bAiControl && !IsDragSteering()`), else DoAISteering (the full angle of the
//    tires at once). With Super Steer off, for an improved player's car, the IsPlayerSteering hook asks the game's own function with bAiControl (+8) cleared for the
//    moment, so that it says yes whenever a human would be steering by the game's rule (not drag steering). The DoSteering hook then sets the wheel's range that the
//    AI divides its steering angle by (ISuspension::GetMaxSteering, `mSteering.Maximum`, which only DoAISteering writes) to the range a human has at that moment
//    (`LastMaximum` times the tires' steering), so that the AI asks for the angle it can have.
bool g_steered_like_a_human = false;  // the last IsPlayerSteering said yes to a car under AI control

int player_of_human_ai(const void *human_ai) {
    for (int p = 0; human_ai && p < 2; ++p)
        if (g_human_ai[p] == human_ai) return p;
    return -1;
}

using IsPlayerSteeringFn = bool(__fastcall *)(void *, void *);
IsPlayerSteeringFn g_orig_is_player_steering = nullptr;

bool __fastcall hook_is_player_steering(void *self, void *edx) {
    constexpr unsigned kAiControl = 8;  // AIVehicleHuman::bAiControl, seen from the IHumanAI
    const int player = player_of_human_ai(self);
    if (player < 0 || variant_of(player) != Variant::kImproved || settings::player(player).super_steer) return g_orig_is_player_steering(self, edx);
    char *ai_control = static_cast<char *>(self) + kAiControl;
    const char under_ai_control = *ai_control;
    *ai_control = 0;
    const bool result = g_orig_is_player_steering(self, edx);
    *ai_control = under_ai_control;
    if (under_ai_control && result) g_steered_like_a_human = true;
    return result;
}

using DoSteeringFn = void(__fastcall *)(void *, void *, void *, void *, void *);  // SuspensionRacer::DoSteering(state, right, left) 0x69E9E0
DoSteeringFn g_orig_do_steering = nullptr;

void __fastcall hook_do_steering(void *self, void *edx, void *state, void *right, void *left) {
    constexpr unsigned kHumanAi = 0xF4, kTires = 0xA0, kTireSteering = 0x48, kMaximum = 0x128, kLastMaximum = 0x12C;
    constexpr float kAbsoluteMaxSteering = 45.0f;
    char *racer = static_cast<char *>(self);
    void *human_ai = *reinterpret_cast<void **>(racer + kHumanAi);
    const int player = player_of_human_ai(human_ai);
    if (player >= 0) car_stats::note_tires(player, *reinterpret_cast<void **>(racer + kTires));
    g_steered_like_a_human = false;
    g_orig_do_steering(self, edx, state, right, left);
    const bool like_a_human = g_steered_like_a_human;
    g_steered_like_a_human = false;
    if (player >= 0 && variant_of(player) == Variant::kImproved && !settings::player(player).super_steer) {
        static DWORD last_said[2] = {0, 0};  // (a trace, for finding out whether this works in the game; to be taken out once it does)
        const DWORD now = GetTickCount();
        if (now - last_said[player] > 5000) {
            last_said[player] = now;
            g_log.line("ai control: steering check, player %d: under AI control %d, steered like a human %d", player + 1, ai_controlled(human_ai) ? 1 : 0, like_a_human ? 1 : 0);
        }
    }
    if (!like_a_human) return;
    const float tires = *reinterpret_cast<float *>(*reinterpret_cast<char **>(racer + kTires) + kTireSteering);
    *reinterpret_cast<float *>(racer + kMaximum) = std::min(*reinterpret_cast<float *>(racer + kLastMaximum) * tires, kAbsoluteMaxSteering);
}

//  - Stuck: a car that is stuck against something drove into it for 3 s (plus the restart of the window) before reversing:
//    AIActionGetUnstuck::CanBeAttempted compares its timer with the constant 3.0 at 0x890604 (shared with its 3 m distance); for an improved
//    car the timer's comparison (0x41167C) is with 1.5.
float g_unstick_seconds = 3.0f;  // what the stub compares with this time
unsigned __cdecl unstick_seconds_bits(const void *vehicle_ai) {
    const int player = player_of(vehicle_ai);
    g_unstick_seconds = player >= 0 && variant_of(player) == Variant::kImproved ? 1.5f : 3.0f;
    unsigned bits;
    memcpy(&bits, &g_unstick_seconds, 4);
    return bits;
}

//  - Skill: AIVehicleHuman::GetSkill (a constant 1.0) is AIPerpVehicle::GetSkill 0x409340 (base skill + glue skill) for a car an AI drives, the base
//    skill being the setting (feed_racer).
//  - Catch-up: AIVehicleHuman::GetCatchupCheat (a constant 0) is AIPerpVehicle::GetCatchupCheat 0x409390 with the setting on, which the game's own
//    glue code feeds: AIPerpVehicle::Update runs it only when the owner is not a player (the `jne` at 0x429501 after the IsPlayer call), so for a
//    car whose catch-up is on that is let through, and the code runs as it does for an opponent: how far the car is behind the average of the players,
//    as a glue skill that raises the skill (GetSkill) and the acceleration (GetCatchupCheat).
float __cdecl skill_of(const void *self) {
    static int logged = 0;
    const int player = player_of_self(self);
    if (g_debug_log && logged < 4) {
        ++logged;
        g_log.line("ai control: GetSkill asked on %p (vehicle ai %p / %p, human ai %p / %p): player %d", self, g_vehicle_ai[0], g_vehicle_ai[1], g_human_ai[0], g_human_ai[1], player + 1);
    }
    if (player < 0 || !wants_control(player)) return 1.0f;
    return reinterpret_cast<float(__thiscall *)(const void *)>(kPerpGetSkill)(g_vehicle_ai[player]);
}

float __cdecl catchup_of(const void *self) {
    const int player = player_of_self(self);
    if (player < 0 || !wants_control(player) || !settings::player(player).catch_up) return 0.0f;
    return reinterpret_cast<float(__thiscall *)(const void *)>(kPerpGetCatchupCheat)(g_vehicle_ai[player]);
}

unsigned __cdecl glue_allowed(const void *perp_vehicle) {
    const int player = player_of(static_cast<const char *>(perp_vehicle) + kPerpVehicleToVehicleAi);
    static int logged = 0;
    const bool allowed = player >= 0 && wants_control(player) && settings::player(player).catch_up;
    if (allowed && logged < 2) g_log.line("ai control: the catch-up code runs for player %d's AI", player + 1), ++logged;
    return allowed ? 1u : 0u;
}

// The race filter the game gives a car's drive-to nav (AIPerpVehicle::Update: `mDriveToNav->SetRaceFilter(IsRacing())`, the byte at nav + 0x51): with it on, the nav
// can only be put on (WRoadNav::InitAtPoint, so wherever the car is) and walk the segments the race's own route is made of, the ones flagged in race. For the
// improved AI of a player that can be switched off (settings: follow_race_route).
unsigned __cdecl race_filter_of(const void *nav, unsigned wanted) {
    if (!wanted) return 0;
    const int player = improved_player(nav);
    if (player < 0 || settings::player(player).follow_race_route) return wanted;
    static bool logged[2] = {false, false};
    if (!logged[player]) {
        logged[player] = true;
        g_log.line("ai control: player %d's nav does not follow the race route (it may be on any road segment)", player + 1);
    }
    return 0;
}

// Replaces a method that is `fld dword ptr [constant]; ret` with a call of `fn(this)`, which returns the float.
bool replace_float_method(uintptr_t site, const unsigned char (&expected)[6], void *fn) {
    nfsmw::Stub stub(32);
    stub.emit({0x51, 0xB8});  // push ecx (this); mov eax,
    stub.emit_u32(reinterpret_cast<uintptr_t>(fn));
    stub.emit({0xFF, 0xD0, 0x83, 0xC4, 0x04, 0xC3});  // call eax; add esp,4; ret (the float stays in st0)
    stub.done();
    return nfsmw::jump_to_stub(site, expected, 6, stub);
}

void patch_ai() {
    using nfsmw::Stub;
    {
        Stub stub(96);
        const uintptr_t skip = 0x429175, proceed = 0x429064;
        stub.emit({0x84, 0xC0});  // test al, al (the IsPlayer result)
        const size_t not_player = stub.jump_short(0x74);  // je proceed
        stub.emit({0x60, 0xFF, 0x76, 0x40, 0xB8});        // pushad; push [esi+0x40]; mov eax,
        stub.emit_u32(reinterpret_cast<uintptr_t>(&nitrous_allowed));
        stub.emit({0xFF, 0xD0, 0x83, 0xC4, 0x04, 0x89, 0x44, 0x24, 0x1C, 0x61, 0x84, 0xC0});  // call eax; add esp,4; mov [esp+1C],eax; popad; test al,al
        const size_t allowed = stub.jump_short(0x75);                                          // jne proceed
        stub.emit_rel32(0xE9, skip);
        stub.resolve_short(not_player);
        stub.resolve_short(allowed);
        stub.emit_rel32(0xE9, proceed);
        stub.done();
        const unsigned char expected[6] = {0x0F, 0x85, 0x11, 0x01, 0x00, 0x00};
        g_log.line("ai control: nitrous for the improved AI of a player %s", nfsmw::jump_to_stub(0x42905E, expected, 6, stub) ? "enabled" : "NOT patched (the code differs)");
    }
    {
        Stub stub(96);
        const uintptr_t after = 0x4291A6;
        stub.emit({0x60, 0xDD, 0x35});  // pushad; fnsave [state]
        stub.emit_u32(reinterpret_cast<uintptr_t>(g_nitro_fpu_state));
        stub.emit({0x8D, 0x44, 0x24, 0x20, 0x50, 0x56, 0xB8});  // lea eax,[esp+20h] (Update's esp); push eax; push esi (the action); mov eax,
        stub.emit_u32(reinterpret_cast<uintptr_t>(&nitro_decision));
        stub.emit({0xFF, 0xD0, 0x83, 0xC4, 0x08, 0x89, 0x44, 0x24, 0x1C, 0xDD, 0x25});  // call eax; add esp,8; mov [esp+1Ch],eax; frstor [state]
        stub.emit_u32(reinterpret_cast<uintptr_t>(g_nitro_fpu_state));
        stub.emit({0x61, 0x8B, 0x11});  // popad; mov edx,[ecx]
        stub.emit_rel32(0xE9, after);
        stub.done();
        const unsigned char expected[6] = {0x8B, 0x44, 0x24, 0x24, 0x8B, 0x11};
        g_log.line("ai control: nitrous of the improved AI %s", nfsmw::jump_to_stub(0x4291A0, expected, 6, stub) ? "can be decided by the route (Options > AI Driving > Smarter Nitro)" : "NOT patched (the code differs)");
    }
    {
        Stub stub(96);
        const uintptr_t after = 0x411682;
        stub.emit({0x60, 0xFF, 0x76, 0x40, 0xB8});  // pushad; push [esi+0x40]; mov eax,
        stub.emit_u32(reinterpret_cast<uintptr_t>(&unstick_seconds_bits));
        stub.emit({0xFF, 0xD0, 0x83, 0xC4, 0x04, 0x61, 0xD8, 0x1D});  // call eax; add esp,4; popad; fcomp dword ptr [
        stub.emit_u32(reinterpret_cast<uintptr_t>(&g_unstick_seconds));
        stub.emit_rel32(0xE9, after);
        stub.done();
        const unsigned char expected[6] = {0xD8, 0x1D, 0x04, 0x06, 0x89, 0x00};
        g_log.line("ai control: stuck for 1.5 s (3 s for the vanilla one) before reversing %s", nfsmw::jump_to_stub(0x41167C, expected, 6, stub) ? "(patched)" : "NOT patched (the code differs)");
    }
    {
        const unsigned char skill[6] = {0xD9, 0x05, 0x6C, 0x09, 0x89, 0x00}, catchup[6] = {0xD9, 0x05, 0x68, 0x09, 0x89, 0x00};
        g_log.line("ai control: skill of a player's AI %s", replace_float_method(0x435190, skill, reinterpret_cast<void *>(&skill_of)) ? "set from the settings" : "NOT patched (the code differs)");
        g_log.line("ai control: catch-up of a player's AI %s", replace_float_method(0x4351B0, catchup, reinterpret_cast<void *>(&catchup_of)) ? "set from the settings" : "NOT patched (the code differs)");
    }
    {
        Stub stub(96);
        const uintptr_t skip = 0x4296DF, proceed = 0x429507;  // the glue code's end, and its start
        const size_t not_player = stub.jump_short(0x74);  // je proceed (the owner is not a player)
        stub.emit({0x60, 0x56, 0xB8});                    // pushad; push esi (the AIPerpVehicle); mov eax,
        stub.emit_u32(reinterpret_cast<uintptr_t>(&glue_allowed));
        stub.emit({0xFF, 0xD0, 0x83, 0xC4, 0x04, 0x89, 0x44, 0x24, 0x1C, 0x61, 0x84, 0xC0});  // call eax; add esp,4; mov [esp+1C],eax; popad; test al,al
        const size_t allowed = stub.jump_short(0x75);                                          // jne proceed
        stub.emit_rel32(0xE9, skip);
        stub.resolve_short(not_player);
        stub.resolve_short(allowed);
        stub.emit_rel32(0xE9, proceed);
        stub.done();
        const unsigned char expected[6] = {0x0F, 0x85, 0xD8, 0x01, 0x00, 0x00};
        g_log.line("ai control: the game's catch-up code for a player's AI %s", nfsmw::jump_to_stub(0x429501, expected, 6, stub) ? "enabled" : "NOT patched (the code differs)");
    }
    {
        Stub stub(64);
        stub.emit({0x60, 0x0F, 0xB6, 0xD0, 0x52, 0x51, 0xB8});  // pushad; movzx edx,al; push edx (wanted); push ecx (the nav); mov eax,
        stub.emit_u32(reinterpret_cast<uintptr_t>(&race_filter_of));
        stub.emit({0xFF, 0xD0, 0x83, 0xC4, 0x08, 0x89, 0x44, 0x24, 0x1C, 0x61});  // call eax; add esp,8; mov [esp+1C],eax; popad
        stub.emit({0x88, 0x41, 0x51, 0x8B, 0x07});                                // what the patched bytes did: mov [ecx+0x51],al; mov eax,[edi]
        stub.emit_rel32(0xE9, 0x42949B);
        stub.done();
        const unsigned char expected[5] = {0x88, 0x41, 0x51, 0x8B, 0x07};
        g_log.line("ai control: the race route filter of the improved AI's nav %s", nfsmw::jump_to_stub(0x429496, expected, 5, stub) ? "can be switched off (Options > AI Driving > Follow Race Route)" : "NOT patched (the code differs)");
    }
    const nfsmw::Hook hooks[] = {
        {"ai_nitro", "AIActionRace::GetPotentialNOS", 0x407BE0, {0xD9, 0x44, 0x24, 0x04, 0x56, 0xD8, 0x1D}, false, nfsmw::detour(&hook_potential_nos),
         nfsmw::orig(&g_orig_potential_nos)},
        {"ai_performance", "AIActionRace::ComputePotentials", 0x428450, {0xA1, 0x00, 0xE0, 0x91, 0x00, 0x83, 0xEC}, false, nfsmw::detour(&hook_compute_potentials),
         nfsmw::orig(&g_orig_compute_potentials)},
        {"ai_off_path", "AIActionRace::CheckOffPath", 0x414780, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false, nfsmw::detour(&hook_check_off_path),
         nfsmw::orig(&g_orig_check_off_path)},
        {"ai_steer_mode", "AIVehicleHuman::IsPlayerSteering", 0x435240, {0x8A, 0x41, 0x08, 0x84, 0xC0, 0x75, 0x15}, false, nfsmw::detour(&hook_is_player_steering),
         nfsmw::orig(&g_orig_is_player_steering)},
        {"ai_steer_range", "SuspensionRacer::DoSteering", 0x69E9E0, {0x83, 0xEC, 0x20, 0x56, 0x8B, 0xF1, 0x8B}, false, nfsmw::detour(&hook_do_steering),
         nfsmw::orig(&g_orig_do_steering)},
    };
    nfsmw::install_hooks(hooks, std::size(hooks), g_log, nullptr, "ai nitrous and steering");
}

}  // namespace

void *drive_to_nav(void *vehicle_ai) { return vehicle_ai ? reinterpret_cast<GetFn>((*static_cast<void ***>(vehicle_ai))[0x48 / 4])(vehicle_ai) : nullptr; }

const float *car_position(void *vehicle_ai) {
    void *body = body_of(vehicle_ai);
    return body ? reinterpret_cast<const float *(__thiscall *)(void *)>((*static_cast<void ***>(body))[0x20 / 4])(body) : nullptr;  // IRigidBody::GetPosition
}

void *racer_info(int player) { return player >= 0 && player < 2 ? racer_info_of(player) : nullptr; }

int improved_player_near(const float point[3], float radius) {
    for (int player = 0; player < 2; ++player) {
        void *ai = g_vehicle_ai[player];
        if (!ai || variant_of(player) != Variant::kImproved) continue;
        const float *at = car_position(ai);
        if (!at) continue;
        const float dx = at[0] - point[0], dy = at[1] - point[1], dz = at[2] - point[2];
        if (dx * dx + dy * dy + dz * dz <= radius * radius) return player;
    }
    return -1;
}

int improved_player(const void *nav) {
    for (int player = 0; player < 2; ++player) {
        void *ai = g_vehicle_ai[player];
        if (!ai || variant_of(player) != Variant::kImproved || drive_to_nav(ai) != nav) continue;
        return body_of(ai) ? player : -1;
    }
    return -1;
}

bool car_velocity(int player, float velocity[3]) {
    void *body = player >= 0 && player < 2 ? body_of(g_vehicle_ai[player]) : nullptr;
    const float *v = body ? reinterpret_cast<const float *(__thiscall *)(void *)>((*static_cast<void ***>(body))[0x24 / 4])(body) : nullptr;  // IRigidBody::GetLinearVelocity
    if (!v) return false;
    velocity[0] = v[0];
    velocity[1] = v[1];
    velocity[2] = v[2];
    return true;
}

void *driving_ai(int player) {
    if (player < 0 || player > 1 || variant_of(player) == Variant::kOff || settings::player(player).overlay != settings::kOverlayAi || *reinterpret_cast<int *>(kGameFlowState) != 6) return nullptr;
    return g_vehicle_ai[player];  // whether or not the AI drives the car: a human's car has its nav and route too
}

void install() {
    patch_ai();  // always: the settings can hand a car to the AI at any time
    g_log.line("ai control: player 1 %s, player 2 %s (the ini, Options > AI Driving)", settings::control_name(settings::player(0).control), settings::control_name(settings::player(1).control));
}

void frame() { on_frame(); }

}  // namespace improved_ai_opponents::control

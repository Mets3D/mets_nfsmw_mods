/*
 * The starting grid of a race with more than four cars. A race starts like this (as `log_racers` and a trace on `PVehicle::SetVehicleOnGround` 0x685810 show):
 * `CreateCars` puts player 1 at the race's start; `Game_InitRacers` (0x61DBE0; its argument is the race's start: position at +0x28, the way the race goes at +0x34)
 * puts every opponent on the road with a WRoadNav that starts at the start and walks *backwards* (`IncNavPosition`, 7 m for each opponent, 1.5 m to the left or right of
 * the nav, alternating: a zigzag, one car to a row); and for the first four racers a Quick Race then moves the cars with `SuspensionSpline::SetNISPosition` (a virtual
 * method, 0x69110C or so, called from the intro cutscene's code; the matrices are the cutscene's data), which is why the layout depends on the race: a 2 by 2 grid in a
 * circuit or a sprint (the player and the first opponent in the rear row, 10.25 m behind the start, the next two in the front row, 2.75 m behind, rows 7.5 m apart, each
 * row 3.3 m wide [tested: positions relative to the start]), a single row of four in a drag race or a lap knockout. The game has no placement for a fifth racer: it
 * stays where `Game_InitRacers` put it. A split screen race has no cutscene: its players 1 and 2 go to the left-most and right-most starting lane
 * (`PlaceSplitScreenPlayersAtStart` 0x60D820, called again when the race is restarted) and the others are where the code above put them.
 * `Game_SaveStartPositions` (0x604E90: after all of this, before it saves the positions that a reset goes back to) is hooked here to lay the racers out the same way for
 * any number of them. A restart does not call it: `PlaceSplitScreenPlayersAtStart` is called again and saves the lanes as the positions of players 1 and 2 itself
 * (`GRacerInfo::SetStartPosition` 0x5DC750, +0x170 position and +0x180 direction), and `Game_RestoreStartPositions` (0x60AA50) then puts every racer back at its saved
 * position; so that function is hooked too, and the placement writes the saved position of each car it places.
 * A split screen race that has played the cutscene (race_intro.cpp) is like a one player race, except that the game puts players 1 and 2 on the starting lanes after it:
 * the first four racers are put back where the cutscene left them (noted when it ended).
 * A one player race: the cutscene has placed the first four, and
 * the others get rows behind them as wide as the cutscene's rear row (the cars of the first four are read: how far behind the start the rearmost are, and where across the
 * road they stand), 7.5 m apart. A split screen race has no cutscene to read: racer k (the order of `GRaceStatus`: the players, then the opponents) has slot k, slots 0 to 3
 * being the places of the 2 by 2 grid above (rear left, rear right, front left, front right) and each pair after them a row 7.5 m further back. Rows follow the road (the
 * same nav as `Game_InitRacers`). Without a road at the start (the nav is not near it) nothing is changed.
 * A tollbooth race is the exception: the game's cutscene for it (IntroNisToll01) leaves the first two racers 1.6 m apart (the cars of a two player race stand on each
 * other), so its cars ignore the cutscene's poses and stand in rows across the selectable lanes of the road at the start (`start_lanes`, `plan_lane_grid`).
 * Who stands where (the Start row of the race options, race_rows.cpp): the places above are slots, slot k racer k (the players first) for Players Front; Players Rear
 * puts the players in the slots furthest behind the start and the opponents in front of them; Random shuffles everybody. The choice is made once for each race, so a
 * restart puts everybody back in the same slots. While the intro cutscene is still to come (race_intro::expected) everybody stands as for Players Front, because the cutscene
 * moves the first four racers into its grid, over whoever stands there; the choice is made when it is over, and saves the places a restart goes back to.
 * The cutscene moves the first four racers only; the opponents after them would stand in its scene (and get bumped by its cars), so while it plays they are out of the
 * world (`IVehicle::Deactivate`, as the game takes out a knocked out opponent) and come back (`Activate`) at their saved start places when it is over.
 */
#include "start_grid.h"

#include <malloc.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

#include "ai_in_split_screen.h"
#include "race_end.h"
#include "racers.h"
#include "hooks.h"
#include "max_players.h"
#include "more_players.h"
#include "players.h"
#include "race_intro.h"
#include "split_screen_support.h"

namespace split_screen_support::start_grid {

namespace {

constexpr uintptr_t kInitRacers = 0x61DBE0, kSaveStartPositions = 0x604E90, kRoadNavConstructor = 0x77B9E0, kRoadNavDestructor = 0x76E220, kInitAtPoint = 0x7862D0, kIncNavPosition = 0x77BBA0,
                    kFind = 0x5D59F0;
constexpr unsigned kMarkerPosition = 0x28, kMarkerDirection = 0x34;  // the argument of Game_InitRacers
constexpr unsigned kNavType = 0x78, kNavPosition = 0x98, kNavForward = 0xBC, kNavSize = 0x400;  // WRoadNav (PC): 0x78 is set to 2 as Game_InitRacers does
constexpr unsigned kVehicleInterface = 0x4040E0, kSetVehicleOnGroundSlot = 0x68 / 4;  // IVehicle's handle for Find, and its SetVehicleOnGround(position, direction)
constexpr unsigned kActivateSlot = 0x80 / 4, kDeactivateSlot = 0x84 / 4;              // IVehicle
constexpr unsigned kSavedPosition = 0x170, kSavedDirection = 0x180;                  // GRacerInfo: where a restart puts the racer
constexpr float kFrontRowBehind = 2.75f;  // metres behind the start: the front row of the game's grid
constexpr float kRowSpacing = 7.5f;       // between rows
constexpr float kHalfWidth = 1.65f;       // each car of a row is this far from the road's line (a row is 3.3 m wide)
constexpr float kNavStep = 1.5f;          // the nav moves by this much until a row's distance is reached (its own steps are not exact)
constexpr float kMaxFromStart = 30.0f;    // the nav has found a road when it is this near the start (m)
constexpr int kCutsceneRacers = 4;        // the racers the Quick Race cutscene places
constexpr uintptr_t kPlaceSplitScreenPlayers = 0x60D820, kRestoreStartPositions = 0x60AA50, kSetStartPosition = 0x5DC750, kGetRaceType = 0x5FAA20;
constexpr unsigned kRaceParameters = 0x1968;  // GRaceStatus: the race's GRaceParameters
constexpr int kRaceDrag = 2, kRaceKnockout = 3, kRaceTollbooth = 4;  // GRace::Type
constexpr uintptr_t kSetPathType = 0x7658A0, kSnapToSelectableLane = 0x77BA80, kIncLane = 0x778360;  // WRoadNav, as PlaceSplitScreenPlayersAtStart uses them
constexpr unsigned kNavLaneType = 0x80;  // set to 6 (the selectable lanes) there
constexpr int kMaxLanes = 12;
constexpr int kMaxRowWidth = kCutsceneRacers;
constexpr float kSingleRowBehind = 3.1f;  // a drag race's row of four (the laterals are from the drag race of one test; the fourth is a guess, a knockout's are a guess)
constexpr float kSingleRowLateral[kMaxRowWidth] = {4.4f, 1.4f, -1.7f, -4.8f};
constexpr float kSameRow = 3.0f;          // cars of the cutscene this close to the rearmost (along the road) are its rear row

void (*g_orig_init_racers)(void *);
void (*g_orig_save_start_positions)();
void (*g_orig_place_split_screen_players)();
void (*g_orig_restore_start_positions)();

float g_start[3], g_direction[3];  // the race's start, noted by Game_InitRacers
bool g_start_known = false;

// The rear row of the cutscene's grid, read from the first four racers once for each race.
struct Layout {
    bool known;
    int width;
    float rear_behind;               // metres behind the start
    float lateral[kMaxRowWidth];     // metres to the left of the road's line, from the left-most car
};
Layout g_cutscene;

float horizontal_distance(const float *a, const float *b) { return hypotf(a[0] - b[0], a[2] - b[2]); }

// The IVehicle of a simable: its interface list answers for the vehicle handle.
void *vehicle_of(void *simable) {
    if (!simable) return nullptr;
    void *interfaces = *reinterpret_cast<void **>(static_cast<char *>(simable) + 4);
    return reinterpret_cast<void *(__thiscall *)(void *, void *)>(kFind)(interfaces, reinterpret_cast<void *>(kVehicleInterface));
}

void put_on_ground(void *vehicle, const float *position, const float *direction) {
    void **vtable = *reinterpret_cast<void ***>(vehicle);
    reinterpret_cast<void(__fastcall *)(void *, void *, const float *, const float *)>(vtable[kSetVehicleOnGroundSlot])(vehicle, nullptr, position, direction);
}

constexpr uintptr_t kRaceStatus = 0x91E000;  // (the racers: racers.h)

// GRacerInfo::SetStartPosition(position, direction): where a restart puts the racer back.
void set_start_position(const unsigned char *info, const float *position, const float *direction) {
    reinterpret_cast<void(__thiscall *)(const void *, const float *, const float *)>(kSetStartPosition)(info, position, direction);
}

// The type of the race that is starting (GRace::Type), -1 when there is none.
int race_type(const unsigned char *status) {
    void *parameters = *reinterpret_cast<void *const *>(status + kRaceParameters);
    return parameters ? reinterpret_cast<int(__thiscall *)(void *)>(kGetRaceType)(parameters) : -1;
}

// Where a racer stands and which way it faces.
struct Pose {
    float position[3], forward[3];
};
Pose g_cutscene_poses[kCutsceneRacers];
bool g_have_poses = false;  // the cutscene of this race has ended and the first four racers' poses are noted

Start g_start_choice = Start::kPlayersFront;
std::vector<int> g_hidden;  // the racers out of the world while the intro cutscene plays
std::vector<int> g_racer_of_slot;  // this race's racer in each slot; made at the first placement of the race

// `depth`: how far behind the start each slot is.
void assign_slots(int count, const std::vector<float> &depth) {
    g_racer_of_slot.resize(count);
    std::iota(g_racer_of_slot.begin(), g_racer_of_slot.end(), 0);
    if (!players::split_screen() || g_start_choice == Start::kPlayersFront) return;
    if (g_start_choice == Start::kRandom) {
        std::mt19937 random(GetTickCount());
        std::shuffle(g_racer_of_slot.begin(), g_racer_of_slot.end(), random);
    } else {
        std::vector<int> slots(count);
        std::iota(slots.begin(), slots.end(), 0);
        std::stable_sort(slots.begin(), slots.end(), [&](int a, int b) { return depth[a] < depth[b]; });
        const int humans = std::min(more_players::race_players(), count);
        int next = 0;
        for (int r = humans; r < count; ++r) g_racer_of_slot[slots[next++]] = r;
        for (int r = 0; r < humans; ++r) g_racer_of_slot[slots[next++]] = r;
    }
    char text[160];
    int length = 0;
    for (int k = 0; k < count && length < static_cast<int>(sizeof(text)) - 8; ++k) length += snprintf(text + length, sizeof(text) - length, " %d", g_racer_of_slot[k] + 1);
    g_log.line("start grid: %s, the racers of the slots front to back in the game's order:%s", g_start_choice == Start::kRandom ? "random" : "players rear", text);
}

// A racer's position (its simable's rigid body: virtual +0x54 GetRigidBody, whose virtual +0x20 gives the position).
bool racer_pose(const unsigned char *info, Pose &pose);
bool racer_position(const unsigned char *info, float *position) {
    void *simable = racers::simable(info);
    if (!simable) return false;
    void **simable_vtable = *reinterpret_cast<void ***>(simable);
    void *body = reinterpret_cast<void *(__fastcall *)(void *, void *)>(simable_vtable[0x54 / 4])(simable, nullptr);
    if (!body) return false;
    void **vtable = *reinterpret_cast<void ***>(body);
    memcpy(position, reinterpret_cast<const float *(__fastcall *)(void *, void *)>(vtable[0x20 / 4])(body, nullptr), 3 * sizeof(float));
    return true;
}

bool racer_pose(const unsigned char *info, Pose &pose) {
    void *simable = racers::simable(info);
    if (!simable) return false;
    void **simable_vtable = *reinterpret_cast<void ***>(simable);
    void *body = reinterpret_cast<void *(__fastcall *)(void *, void *)>(simable_vtable[0x54 / 4])(simable, nullptr);
    if (!body) return false;
    void **vtable = *reinterpret_cast<void ***>(body);
    memcpy(pose.position, reinterpret_cast<const float *(__fastcall *)(void *, void *)>(vtable[0x20 / 4])(body, nullptr), 3 * sizeof(float));
    float v[4] = {};
    reinterpret_cast<void(__fastcall *)(void *, void *, float *)>(vtable[0x34 / 4])(body, nullptr, v);  // the forward vector
    memcpy(pose.forward, v, 3 * sizeof(float));
    return true;
}

// A car's place: metres behind the start along the road, and metres to the left of the road's line.
struct Placement {
    int racer;
    float behind, lateral;
    bool from_start = false;  // lateral is measured from the race's start, not from the road's line the nav walks along
};

// Slot k of a split screen grid: the row (0 is the front one) and the side (+1 left of the way the race goes, -1 right): the order of the game's 2 by 2 grid for the first four.
struct Slot {
    int row, side;
};

Slot slot_of(int k) {
    switch (k) {
        case 0: return {1, +1};
        case 1: return {1, -1};
        case 2: return {0, +1};
        case 3: return {0, -1};
    }
    return {2 + (k - kCutsceneRacers) / 2, k % 2 == 0 ? +1 : -1};
}

// Reads the grid the cutscene made from the first four racers: the cars no further than kSameRow in front of the rearmost one are the rear row.
bool read_cutscene_layout(const unsigned char *status, const float *start, const float *direction) {
    const float length = hypotf(direction[0], direction[2]);
    if (length < 0.01f) return false;
    const float dx = direction[0] / length, dz = direction[2] / length;
    float behind[kCutsceneRacers], lateral[kCutsceneRacers], rear = -1e9f;
    for (int k = 0; k < kCutsceneRacers; ++k) {
        float p[3];
        if (!racer_position(status + 0x18 + k * 0x194, p)) return false;
        const float rx = p[0] - start[0], rz = p[2] - start[2];
        behind[k] = -(rx * dx + rz * dz);
        lateral[k] = -rx * dz + rz * dx;
        rear = std::max(rear, behind[k]);
    }
    g_cutscene.width = 0;
    for (int k = 0; k < kCutsceneRacers; ++k)
        if (behind[k] >= rear - kSameRow) g_cutscene.lateral[g_cutscene.width++] = lateral[k];
    std::sort(g_cutscene.lateral, g_cutscene.lateral + g_cutscene.width, [](float a, float b) { return a > b; });
    g_cutscene.rear_behind = rear;
    g_cutscene.known = true;
    g_log.line("start grid: race type %d, the cutscene's rear row has %d cars, %.1f m behind the start, at %.1f %.1f %.1f %.1f m to the left", race_type(status), g_cutscene.width, rear,
               g_cutscene.lateral[0], g_cutscene.width > 1 ? g_cutscene.lateral[1] : 0.0f, g_cutscene.width > 2 ? g_cutscene.lateral[2] : 0.0f,
               g_cutscene.width > 3 ? g_cutscene.lateral[3] : 0.0f);
    return true;
}

bool inc_lane(void *nav, int step) { return reinterpret_cast<bool(__thiscall *)(void *, int)>(kIncLane)(nav, step); }

// The selectable lanes of the road at the race's start (the lanes PlaceSplitScreenPlayersAtStart picks from: it puts players 1 and 2 on the outermost ones), as metres to the
// left of the start, the left-most first.
std::vector<float> start_lanes(const float *start, const float *direction) {
    std::vector<float> lanes;
    const float length = hypotf(direction[0], direction[2]);
    if (length < 0.01f) return lanes;
    const float left_x = -direction[2] / length, left_z = direction[0] / length;
    unsigned char *nav = static_cast<unsigned char *>(_aligned_malloc(kNavSize, 16));
    if (!nav) return lanes;
    memset(nav, 0, kNavSize);
    reinterpret_cast<void(__thiscall *)(void *)>(kRoadNavConstructor)(nav);
    *reinterpret_cast<int *>(nav + kNavType) = 2;
    reinterpret_cast<void(__thiscall *)(void *, int)>(kSetPathType)(nav, 4);
    *reinterpret_cast<int *>(nav + kNavLaneType) = 6;
    reinterpret_cast<void(__thiscall *)(void *, const float *, const float *, bool, float)>(kInitAtPoint)(nav, start, direction, false, 1.0f);
    reinterpret_cast<void(__thiscall *)(void *)>(kSnapToSelectableLane)(nav);
    const float *position = reinterpret_cast<const float *>(nav + kNavPosition);
    for (int i = 0; i < kMaxLanes && inc_lane(nav, -1); ++i) {}  // to the lane on one side, then across to the other
    auto record = [&] { lanes.push_back((position[0] - start[0]) * left_x + (position[2] - start[2]) * left_z); };
    record();
    for (int i = 0; i < kMaxLanes && inc_lane(nav, 1); ++i) record();
    reinterpret_cast<void(__thiscall *)(void *)>(kRoadNavDestructor)(nav);
    _aligned_free(nav);
    std::sort(lanes.begin(), lanes.end(), [](float a, float b) { return a > b; });
    return lanes;
}

// A grid across the road's lanes: rows of as many cars as there are lanes (a row with fewer cars uses lanes spread over the road, the outer ones for two), the first row
// kFrontRowBehind behind the start and the others kRowSpacing apart.
std::vector<Placement> plan_lane_grid(int count, const std::vector<float> &lanes) {
    std::vector<Placement> out;
    const int lane_count = static_cast<int>(lanes.size()), width = std::min(lane_count, count);
    for (int k = 0; k < count; ++k) {
        const int row = k / width, col = k % width, cars = std::min(width, count - row * width);
        const int lane = cars == 1 ? (lane_count - 1) / 2 : (col * (lane_count - 1) + (cars - 1) / 2) / (cars - 1);
        out.push_back({k, kFrontRowBehind + kRowSpacing * row, lanes[lane], true});
    }
    return out;
}

std::vector<Placement> plan_placements(const unsigned char *status, int count, const float *start, const float *direction) {
    std::vector<Placement> out;
    if (players::split_screen() && race_type(status) == kRaceTollbooth) {  // the cutscene's grid is made for one player: the cars of two players stand on each other
        const std::vector<float> lanes = start_lanes(start, direction);
        g_log.line("start grid: tollbooth, %d selectable lanes at the start (m to the left of it: %.1f %.1f %.1f %.1f %.1f %.1f)", static_cast<int>(lanes.size()),
                   lanes.size() > 0 ? lanes[0] : 0.0f, lanes.size() > 1 ? lanes[1] : 0.0f, lanes.size() > 2 ? lanes[2] : 0.0f, lanes.size() > 3 ? lanes[3] : 0.0f,
                   lanes.size() > 4 ? lanes[4] : 0.0f, lanes.size() > 5 ? lanes[5] : 0.0f);
        if (lanes.size() >= 2) return plan_lane_grid(count, lanes);
    }
    if (players::split_screen() && (!g_have_poses || race_type(status) == kRaceTollbooth)) {  // no cutscene: the 2 by 2 grid of the game, which is what most of them make
        if (count <= nfsmw::kPlayersPerRound) return out;
        // The cutscene's layouts that were seen (a drag race and a lap knockout start in a single row of four, the others in a 2 by 2 grid); there is no cutscene to read.
        const int type = race_type(status);
        const bool single_row = type == kRaceDrag || type == kRaceKnockout;
        const Layout row = {true, kCutsceneRacers, kSingleRowBehind, {kSingleRowLateral[0], kSingleRowLateral[1], kSingleRowLateral[2], kSingleRowLateral[3]}};
        const Layout grid = {true, 2, kFrontRowBehind + kRowSpacing, {kHalfWidth, -kHalfWidth}};
        const Layout &rows = single_row ? row : grid;
        for (int k = 0; k < count; ++k) {
            if (k < kCutsceneRacers && single_row) {
                out.push_back({k, kSingleRowBehind, kSingleRowLateral[k]});
            } else if (k < kCutsceneRacers) {
                const Slot slot = slot_of(k);
                out.push_back({k, kFrontRowBehind + kRowSpacing * slot.row, slot.side * kHalfWidth});
            } else {
                const int extra_row = (k - kCutsceneRacers) / rows.width + 1;
                out.push_back({k, rows.rear_behind + kRowSpacing * extra_row, rows.lateral[(k - kCutsceneRacers) % rows.width]});
            }
        }
        return out;
    }
    if (count <= kCutsceneRacers) return out;
    if (!g_cutscene.known && !read_cutscene_layout(status, start, direction)) {
        g_cutscene.width = 2;
        g_cutscene.rear_behind = kFrontRowBehind + kRowSpacing;
        g_cutscene.lateral[0] = kHalfWidth;
        g_cutscene.lateral[1] = -kHalfWidth;
        g_cutscene.known = true;
        g_log.line("start grid: the cutscene's grid could not be read: 2 cars to a row");
    }
    for (int k = kCutsceneRacers; k < count; ++k) {
        const int row = (k - kCutsceneRacers) / g_cutscene.width + 1;
        out.push_back({k, g_cutscene.rear_behind + kRowSpacing * row, g_cutscene.lateral[(k - kCutsceneRacers) % g_cutscene.width]});
    }
    return out;
}

void place_racers(const float *start, const float *direction) {
    const unsigned char *status = *reinterpret_cast<unsigned char **>(kRaceStatus);
    if (!status) return;
    const int count = *reinterpret_cast<const int *>(status + 0x1958);
    const bool poses = g_have_poses && !(players::split_screen() && race_type(status) == kRaceTollbooth);
    std::vector<Placement> placements = plan_placements(status, count, start, direction);
    // The cutscene is still to come and moves the first four racers into its own grid: whoever stands in those places now would be under them. Until it is over everybody
    // stands as for Players Front, and the Start row's choice is made after it, which also saves the places a restart goes back to.
    std::vector<int> front_order;
    if (race_intro::expected()) {
        front_order.resize(count);
        std::iota(front_order.begin(), front_order.end(), 0);
        if (players::split_screen() && g_start_choice != Start::kPlayersFront) g_log.line("start grid: the intro cutscene is to come: the racers stand in the order of Players Front until it is over");
    }
    const std::vector<int> &racer_of_slot = front_order.empty() ? g_racer_of_slot : front_order;
    if (front_order.empty() && static_cast<int>(g_racer_of_slot.size()) != count) {
        std::vector<float> depth(count, 0.0f);
        const float length = hypotf(direction[0], direction[2]);
        for (int k = 0; poses && length > 0.01f && k < count && k < kCutsceneRacers; ++k)
            depth[k] = ((start[0] - g_cutscene_poses[k].position[0]) * direction[0] + (start[2] - g_cutscene_poses[k].position[2]) * direction[2]) / length;
        for (const Placement &p : placements) depth[p.racer] = p.behind;
        assign_slots(count, depth);
    }
    if (poses) {  // the first four slots are where the cutscene left its racers (the game moved players 1 and 2 to the starting lanes)
        for (int k = 0; k < count && k < kCutsceneRacers; ++k) {
            const unsigned char *info = status + 0x18 + racer_of_slot[k] * 0x194;
            void *vehicle = vehicle_of(racers::simable(info));
            if (!vehicle) continue;
            put_on_ground(vehicle, g_cutscene_poses[k].position, g_cutscene_poses[k].forward);
            set_start_position(info, g_cutscene_poses[k].position, g_cutscene_poses[k].forward);
        }
    }
    if (placements.empty()) return;
    for (Placement &p : placements) p.racer = racer_of_slot[p.racer];
    std::sort(placements.begin(), placements.end(), [](const Placement &a, const Placement &b) { return a.behind < b.behind; });
    unsigned char *nav = static_cast<unsigned char *>(_aligned_malloc(kNavSize, 16));
    if (!nav) return;
    memset(nav, 0, kNavSize);
    reinterpret_cast<void(__thiscall *)(void *)>(kRoadNavConstructor)(nav);
    *reinterpret_cast<int *>(nav + kNavType) = 2;
    const float back[3] = {-direction[0], -direction[1], -direction[2]};  // the nav walks against the way the race goes
    reinterpret_cast<void(__thiscall *)(void *, const float *, const float *, bool, float)>(kInitAtPoint)(nav, start, back, false, 1.0f);
    const float *position = reinterpret_cast<const float *>(nav + kNavPosition), *forward = reinterpret_cast<const float *>(nav + kNavForward);
    if (!std::isfinite(position[0]) || horizontal_distance(position, start) > kMaxFromStart) {
        g_log.line("start grid: no road at the start (the nav is at (%.1f, %.1f, %.1f)): the racers stay where they were made", position[0], position[1], position[2]);
    } else {
        float row_start[3] = {position[0], position[1], position[2]};
        const float direction_length = hypotf(direction[0], direction[2]);
        const float base_lateral = direction_length < 0.01f ? 0.0f : ((position[0] - start[0]) * -direction[2] + (position[2] - start[2]) * direction[0]) / direction_length;
        float walked = 0;  // how far behind the start the nav is (the sum of the legs: each leg is a straight distance from the last stop)
        for (size_t i = 0; i < placements.size(); ++i) {
            if (placements[i].behind > walked + 0.01f) {
                const float wanted = placements[i].behind - walked;
                float along = 0;
                for (int step = 0; step < 64 && along < wanted; ++step) {
                    reinterpret_cast<void(__thiscall *)(void *, float, const float *, float)>(kIncNavPosition)(nav, kNavStep, back, 0.0f);
                    along = horizontal_distance(position, row_start);
                }
                memcpy(row_start, position, sizeof(row_start));
                walked = placements[i].behind;
            }
            // The nav's forward vector points the way it walked (against the race); the cars face the other way, and the sides are across it.
            const float length = hypotf(forward[0], forward[2]);
            const float facing[3] = {-forward[0], -forward[1], -forward[2]};
            const float left[3] = {-facing[2] / length, 0, facing[0] / length};
            void *vehicle = vehicle_of(racers::simable(status + 0x18 + placements[i].racer * 0x194));
            if (!vehicle) continue;
            const float offset = placements[i].lateral - (placements[i].from_start ? base_lateral : 0.0f);
            const float car[3] = {position[0] + left[0] * offset, position[1], position[2] + left[2] * offset};
            put_on_ground(vehicle, car, facing);
            set_start_position(status + 0x18 + placements[i].racer * 0x194, car, facing);
            g_log.line("start grid: racer %d, %.1f m behind the start and %.1f m to the left, at (%.1f, %.1f, %.1f), facing (%.2f, %.2f, %.2f)", placements[i].racer + 1, placements[i].behind,
                       offset, car[0], car[1], car[2], facing[0], facing[1], facing[2]);
        }
    }
    reinterpret_cast<void(__thiscall *)(void *)>(kRoadNavDestructor)(nav);
    _aligned_free(nav);
}

void call_vehicle(void *vehicle, unsigned slot) { reinterpret_cast<void(__fastcall *)(void *, void *)>((*reinterpret_cast<void ***>(vehicle))[slot])(vehicle, nullptr); }

bool is_human(int racer) {
    for (int p = 0; p < nfsmw::kMaxPlayers; ++p)
        if (racers::of_player(more_players::game_player(p)) == racer) return true;
    return false;
}

void hide_extras() {
    const unsigned char *status = *reinterpret_cast<unsigned char **>(kRaceStatus);
    const int count = status ? *reinterpret_cast<const int *>(status + 0x1958) : 0;
    for (int k = kCutsceneRacers; k < count; ++k) {
        void *vehicle = vehicle_of(racers::simable(status + 0x18 + k * 0x194));
        if (!vehicle || is_human(k)) continue;  // (a player's camera and HUD follow their car)
        call_vehicle(vehicle, kDeactivateSlot);
        g_hidden.push_back(k);
    }
    if (!g_hidden.empty()) g_log.line("start grid: %d opponents after the first four are out of the world while the intro cutscene plays", static_cast<int>(g_hidden.size()));
}

void bring_back() {
    const unsigned char *status = *reinterpret_cast<unsigned char **>(kRaceStatus);
    for (const int k : g_hidden) {
        const unsigned char *info = status ? status + 0x18 + k * 0x194 : nullptr;
        void *vehicle = info ? vehicle_of(racers::simable(info)) : nullptr;
        if (!vehicle) continue;
        call_vehicle(vehicle, kActivateSlot);
        const float *position = reinterpret_cast<const float *>(info + kSavedPosition);
        if (position[0] || position[1] || position[2]) put_on_ground(vehicle, position, reinterpret_cast<const float *>(info + kSavedDirection));
    }
    if (!g_hidden.empty()) g_log.line("start grid: the %d opponents are back", static_cast<int>(g_hidden.size()));
    g_hidden.clear();
}

void cutscene_over_impl() {
    bring_back();
    const unsigned char *status = *reinterpret_cast<unsigned char **>(kRaceStatus);
    if (!status || !g_start_known) return;
    const int count = *reinterpret_cast<const int *>(status + 0x1958);
    if (count < kCutsceneRacers) return;
    Pose poses[kCutsceneRacers];
    for (int k = 0; k < kCutsceneRacers; ++k)
        if (!racer_pose(status + 0x18 + k * 0x194, poses[k])) return;
    memcpy(g_cutscene_poses, poses, sizeof(poses));
    g_have_poses = true;
    g_cutscene.known = false;  // (read again from where the cars are)
    read_cutscene_layout(status, g_start, g_direction);
}

void hook_init_racers(void *race_start) {
    ai_in_split_screen::prepare_race();
    race_end::race_started();
    g_orig_init_racers(race_start);
    ai_in_split_screen::racers_made();
    g_start_known = race_start != nullptr;
    g_cutscene.known = false;
    g_have_poses = false;
    g_racer_of_slot.clear();
    g_hidden.clear();  // (the cars of the race before are gone)
    if (!race_start) return;
    memcpy(g_start, static_cast<char *>(race_start) + kMarkerPosition, sizeof(g_start));
    memcpy(g_direction, static_cast<char *>(race_start) + kMarkerDirection, sizeof(g_direction));
}

// Called when the race starts: the positions it saves are the ones a restart goes back to.
void hook_save_start_positions() {
    if (g_start_known) place_racers(g_start, g_direction);
    g_orig_save_start_positions();
}

// Called when the race starts and when it is restarted: players 1 and 2 are put on the starting lanes and their lanes saved as where a restart goes back to, so
// the racers are placed again (this also saves their positions).
void hook_place_split_screen_players() {
    g_orig_place_split_screen_players();
    if (g_start_known) place_racers(g_start, g_direction);
}

// What a restart does: every racer goes back to its saved position. Logged, for the bug reports about cars that start in the wrong places.
void hook_restore_start_positions() {
    bring_back();  // (a restart while the cutscene played)
    const unsigned char *status = *reinterpret_cast<unsigned char **>(kRaceStatus);
    const int count = status ? *reinterpret_cast<const int *>(status + 0x1958) : 0;
    for (int k = 0; k < count; ++k) {
        const unsigned char *info = status + 0x18 + k * 0x194;
        const float *saved = reinterpret_cast<const float *>(info + 0x170);
        float now[3] = {0, 0, 0};
        const bool known = racer_position(info, now);
        g_log.line("restart: racer %d goes from (%.1f, %.1f, %.1f)%s to (%.1f, %.1f, %.1f)", k + 1, now[0], now[1], now[2], known ? "" : " (no body)", saved[0], saved[1], saved[2]);
    }
    g_orig_restore_start_positions();
}

}  // namespace

void cutscene_plays() { hide_extras(); }

void cutscene_over() { cutscene_over_impl(); }

void set_start(Start start) { g_start_choice = start; }

void install() {
    const nfsmw::Hook hooks[] = {
        {"startgrid", "Game_InitRacers", kInitRacers, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B}, false, nfsmw::detour(&hook_init_racers), nfsmw::orig(&g_orig_init_racers)},
        {"startgridsave", "Game_SaveStartPositions", kSaveStartPositions, {0x8B, 0x0D, 0x00, 0xE0, 0x91, 0x00, 0x56}, false, nfsmw::detour(&hook_save_start_positions),
         nfsmw::orig(&g_orig_save_start_positions)},
        {"startgridsplit", "PlaceSplitScreenPlayersAtStart", kPlaceSplitScreenPlayers, {0x6A, 0xFF, 0x68, 0x3B, 0x66, 0x87, 0x00}, false,
         nfsmw::detour(&hook_place_split_screen_players), nfsmw::orig(&g_orig_place_split_screen_players)},
        {"startgridrestore", "Game_RestoreStartPositions", kRestoreStartPositions, {0x8B, 0x0D, 0x74, 0xC6, 0x92, 0x00, 0x85}, false,
         nfsmw::detour(&hook_restore_start_positions), nfsmw::orig(&g_orig_restore_start_positions)},
    };
    nfsmw::install_hooks(hooks, 4, g_log, g_ini, "start grid");
}

}  // namespace split_screen_support::start_grid

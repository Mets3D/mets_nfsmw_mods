/*
 * The starting grid of players 3 and up. The game puts a split screen race's players 1 and 2 where `PlaceSplitScreenPlayersAtStart` (0x60D820, called from `Game_WarpPlayerToTrigger`
 * in split screen mode) puts them: the left-most and right-most starting lane of the start, a little behind the start line. A race with opponents puts
 * them on the road with `Game_InitRacers` (0x61DBE0; its argument is the race's start: position at +0x28, the way the race goes at +0x34): a WRoadNav
 * starts at the start and walks *backwards* along the road (`IncNavPosition`, 7 m for each opponent), and each opponent stands at the nav's position,
 * 1.5 m to the left or right of it (alternating), facing the way the road goes [tested: lateral 1.500, along 0.000 on four tracks]. The cars of players 3
 * and up were made a straight 9 m behind the first two (`more_players.cpp`), which is not on the road in a curve and not in a lane. Here they are put the
 * same way the opponents are, two to a row (1.5 m to each side of the nav, 7 m for each row), behind the rearmost of the others (players 1 and 2, and the
 * opponents when the race has them, see ai_in_split_screen.cpp): the start is
 * noted at `Game_InitRacers` and the cars are put on the road at `Game_SaveStartPositions` (0x604E90, once the game has put players 1 and 2 where they
 * stand and before it saves the positions that a reset goes back to). Without a road at the start (the nav is not near it) the straight rows stay.
 */
#include "start_grid.h"

#include <malloc.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "hooks.h"
#include "max_players.h"
#include "more_players.h"
#include "split_screen_support.h"

namespace split_screen_support::start_grid {

namespace {

constexpr uintptr_t kInitRacers = 0x61DBE0, kSaveStartPositions = 0x604E90, kRoadNavConstructor = 0x77B9E0, kRoadNavDestructor = 0x76E220, kInitAtPoint = 0x7862D0, kIncNavPosition = 0x77BBA0,
                    kFind = 0x5D59F0;
constexpr unsigned kMarkerPosition = 0x28, kMarkerDirection = 0x34;  // the argument of Game_InitRacers
constexpr unsigned kNavType = 0x78, kNavPosition = 0x98, kNavForward = 0xBC, kNavSize = 0x400;  // WRoadNav (PC): 0x78 is set to 2 as Game_InitRacers does
constexpr unsigned kVehicleInterface = 0x4040E0, kSetVehicleOnGroundSlot = 0x68 / 4;  // IVehicle's handle for Find, and its SetVehicleOnGround(position, direction)
constexpr float kRowSpacing = 7.0f;     // metres between the rows
constexpr float kNavStep = 1.5f;        // the nav moves by this much until a row's distance is reached (its own steps are not exact)
constexpr float kHalfLane = 1.5f;       // how far each car of a row is from the nav's position
constexpr float kMaxFromStart = 30.0f;  // the nav has found a road when it is this near the start (m)

void (*g_orig_init_racers)(void *);
void (*g_orig_save_start_positions)();

float g_start[3], g_direction[3];  // the race's start, noted by Game_InitRacers
bool g_start_known = false;

float horizontal_distance(const float *a, const float *b) { return hypotf(a[0] - b[0], a[2] - b[2]); }

// The IVehicle of a LocalPlayer: its car's interface list answers for the vehicle handle.
void *vehicle_of(void *player) {
    void **vtable = *reinterpret_cast<void ***>(player);
    void *simable = reinterpret_cast<void *(__fastcall *)(void *, void *)>(vtable[1])(player, nullptr);  // IPlayer::GetSimable
    if (!simable) return nullptr;
    void *interfaces = *reinterpret_cast<void **>(static_cast<char *>(simable) + 4);
    return reinterpret_cast<void *(__thiscall *)(void *, void *)>(kFind)(interfaces, reinterpret_cast<void *>(kVehicleInterface));
}

void put_on_ground(void *vehicle, const float *position, const float *direction) {
    void **vtable = *reinterpret_cast<void ***>(vehicle);
    reinterpret_cast<void(__fastcall *)(void *, void *, const float *, const float *)>(vtable[kSetVehicleOnGroundSlot])(vehicle, nullptr, position, direction);
}

// The position of a racer's car (GRaceStatus *0x91E000: the count at +0x1958 and the GRacerInfos of 0x194 bytes from +0x18; a racer's simable is found by its
// handle (+0) in the table at 0x92FEB4, the node's +4; the car's rigid body through the simable's virtual table, +0x54 GetRigidBody, whose +0x20 is the position).
constexpr uintptr_t kRaceStatus = 0x91E000, kSimableTable = 0x92FEB4, kSimableTableCount = 0x92FEBC, kTableSearch = 0x5D51F0;

void *racer_simable(const unsigned char *info) {
    const unsigned handle = *reinterpret_cast<const unsigned *>(info);
    if (!handle) return nullptr;
    unsigned char *table = *reinterpret_cast<unsigned char **>(kSimableTable);
    unsigned char *end = table + *reinterpret_cast<int *>(kSimableTableCount) * 8;
    unsigned char *node = reinterpret_cast<unsigned char *(__cdecl *)(unsigned char *, unsigned char *, unsigned)>(kTableSearch)(table, end, handle);
    return node ? *reinterpret_cast<void **>(node + 4) : nullptr;
}

bool simable_position(void *simable, float *position) {
    void **simable_vtable = *reinterpret_cast<void ***>(simable);
    void *body = reinterpret_cast<void *(__fastcall *)(void *, void *)>(simable_vtable[0x54 / 4])(simable, nullptr);
    if (!body) return false;
    void **vtable = *reinterpret_cast<void ***>(body);
    memcpy(position, reinterpret_cast<const float *(__fastcall *)(void *, void *)>(vtable[0x20 / 4])(body, nullptr), 3 * sizeof(float));
    return true;
}

// How far behind the start (along the way the race goes) the rearmost car stands, not counting the cars of the extra players (players 1 and 2 and the
// opponents the game made are the racers that count).
float rearmost_racer(const float *start, const float *direction) {
    void *extras[nfsmw::kMaxExtras] = {};
    for (int i = 0; i < nfsmw::kMaxExtras; ++i)
        if (void *player = more_players::extra_player(i)) {
            void **vtable = *reinterpret_cast<void ***>(player);
            extras[i] = reinterpret_cast<void *(__fastcall *)(void *, void *)>(vtable[1])(player, nullptr);  // IPlayer::GetSimable
        }
    const unsigned char *status = *reinterpret_cast<unsigned char **>(kRaceStatus);
    if (!status) return 0;
    const int count = *reinterpret_cast<const int *>(status + 0x1958);
    float behind = 0;
    for (int i = 0; i < count; ++i) {
        void *simable = racer_simable(status + 0x18 + i * 0x194);
        bool extra = false;
        for (void *e : extras) extra = extra || (e && e == simable);
        float position[3];
        if (!simable || extra || !simable_position(simable, position)) continue;
        behind = std::max(behind, (start[0] - position[0]) * direction[0] + (start[2] - position[2]) * direction[2]);
    }
    return behind;
}

void place_extras(const float *start, const float *direction) {
    const int extras = more_players::extra_player_count();
    if (!extras) return;
    unsigned char *nav = static_cast<unsigned char *>(_aligned_malloc(kNavSize, 16));
    if (!nav) return;
    memset(nav, 0, kNavSize);
    reinterpret_cast<void(__thiscall *)(void *)>(kRoadNavConstructor)(nav);
    *reinterpret_cast<int *>(nav + kNavType) = 2;
    const float back[3] = {-direction[0], -direction[1], -direction[2]};  // the nav walks against the way the race goes
    reinterpret_cast<void(__thiscall *)(void *, const float *, const float *, bool, float)>(kInitAtPoint)(nav, start, back, false, 1.0f);
    const float *position = reinterpret_cast<const float *>(nav + kNavPosition), *forward = reinterpret_cast<const float *>(nav + kNavForward);
    if (!std::isfinite(position[0]) || horizontal_distance(position, start) > kMaxFromStart) {
        g_log.line("start grid: no road at the start (the nav is at (%.1f, %.1f, %.1f)): the cars of the extra players stay where they were made", position[0], position[1],
                   position[2]);
    } else {
        const float first_row = rearmost_racer(start, direction) + kRowSpacing;  // metres behind the start
        float row_start[3] = {position[0], position[1], position[2]};
        for (int row = 0; row * 2 < extras; ++row) {
            const float wanted = row == 0 ? first_row : kRowSpacing;
            float along = 0;
            for (int step = 0; step < 32 && along < wanted; ++step) {
                reinterpret_cast<void(__thiscall *)(void *, float, const float *, float)>(kIncNavPosition)(nav, kNavStep, back, 0.0f);
                along = horizontal_distance(position, row_start);
            }
            memcpy(row_start, position, sizeof(row_start));
            // The nav's forward vector points the way it walked (against the race); the cars face the other way, and the sides are across it.
            const float length = hypotf(forward[0], forward[2]);
            const float side[3] = {-forward[2] / length, 0, forward[0] / length};
            const float facing[3] = {-forward[0], -forward[1], -forward[2]};
            const int in_row = std::min(2, extras - row * 2);
            for (int i = 0; i < in_row; ++i) {
                const int extra = row * 2 + i;
                void *player = more_players::extra_player(extra);
                void *vehicle = player ? vehicle_of(player) : nullptr;
                if (!vehicle) continue;
                const float offset = in_row == 1 ? 0.0f : (i == 0 ? kHalfLane : -kHalfLane);
                const float car[3] = {position[0] + side[0] * offset, position[1], position[2] + side[2] * offset};
                put_on_ground(vehicle, car, facing);
                g_log.line("start grid: player %d in row %d at (%.1f, %.1f, %.1f), %.1f m from the road's line, facing (%.2f, %.2f, %.2f)", nfsmw::kPlayersPerRound + extra + 1,
                           row + 1, car[0], car[1], car[2], offset, facing[0], facing[1], facing[2]);
            }
        }
    }
    reinterpret_cast<void(__thiscall *)(void *)>(kRoadNavDestructor)(nav);
    _aligned_free(nav);
}

void hook_init_racers(void *race_start) {
    g_orig_init_racers(race_start);
    g_start_known = race_start != nullptr;
    if (!race_start) return;
    memcpy(g_start, static_cast<char *>(race_start) + kMarkerPosition, sizeof(g_start));
    memcpy(g_direction, static_cast<char *>(race_start) + kMarkerDirection, sizeof(g_direction));
}

void hook_save_start_positions() {
    if (g_start_known) place_extras(g_start, g_direction);
    g_start_known = false;
    g_orig_save_start_positions();
}

}  // namespace

void install() {
    const nfsmw::Hook hooks[] = {
        {"startgrid", "Game_InitRacers", kInitRacers, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B}, false, nfsmw::detour(&hook_init_racers), nfsmw::orig(&g_orig_init_racers)},
        {"startgridsave", "Game_SaveStartPositions", kSaveStartPositions, {0x8B, 0x0D, 0x00, 0xE0, 0x91, 0x00, 0x56}, false, nfsmw::detour(&hook_save_start_positions),
         nfsmw::orig(&g_orig_save_start_positions)},
    };
    nfsmw::install_hooks(hooks, 2, g_log, g_ini, "start grid");
}

}  // namespace split_screen_support::start_grid

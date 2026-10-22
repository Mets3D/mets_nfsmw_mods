// The game's route search (AStarSearch::Service 0x788040) costs a step along a road segment as its length: it takes the shortest way, whatever the road is like and
// whatever the car can do. For the cars whose AI is "improved" (ai_control.cpp) a step costs the TIME it takes, in metres: the time times the top speed, so that a
// step at the top speed costs exactly its length and a slower one more (the search's heuristic, the straight distance to the goal, still never overestimates).
// The time comes from a speed carried along the route:
//  - A segment has a limit speed: the speed at which the car can follow the flattest line through the segment's lanes (geometry::corner_limit gives the line's
//    radius, car_stats::cornering_speed the speed: from the grip of the car's tires, and its top speed as the most). A straight, or a bend that a wide road lets a car
//    straighten, has the top speed.
//  - A car enters a segment at the speed it left the last one. Faster than the segment's limit, it brakes and loses the time the braking takes over the
//    time the same distance would take at the entry speed; then it accelerates along the segment (the car's own acceleration at each speed) up to the limit. Slower, it
//    simply accelerates. The car is the player's: its top speed, acceleration and grip are the game's numbers for it (car_stats.cpp). The segment's time is that of its length driven so, and the speed it leaves at is the next segment's entry speed.
//  - The first segments of a search are entered at the car's own speed, so a route planned on the grid assumes the car will accelerate to what it can do by the time
//    it gets to a corner, and a corner that comes at the end of a straight costs what slowing down for it takes: the same
//    corner costs little to a car that is crawling anyway.
// The speed at the end of a step depends on the route that led there, which a search over (node, segment) does not remember: it is kept with each of those (the
// route that reached it by the lowest cost; `g_states`), which is an approximation, not an exact optimum.
// The step's cost is the game's `actual_cost = parent_cost + length` (the fmul at 0x788206 is where the length is made): a stub there adds our extra to the length.
// Also added: a segment run against the race's direction (kWrongWayWeight), and the shortcut setting. The search is for one nav: the extra is only added when it is the drive-to nav of a car set to "improved"; any other search (the vanilla
// AI's, the opponents', the cops') gets nothing added.
#include "path_cost.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <unordered_map>

#include "ai_control.h"
#include "car_stats.h"
#include "hooks.h"
#include "patch.h"
#include "race_area.h"
#include "road_geometry.h"
#include "road_network.h"
#include "settings.h"
#include "improved_ai_opponents.h"

namespace improved_ai_opponents::path_cost {

namespace {

constexpr unsigned kSearchNav = 0x2C;                                  // AStarSearch: the WRoadNav it is for
constexpr unsigned kNodeParentSlot = 0x8, kNodeSegment = 0xA, kNodeRoadNode = 0xC, kNodeCost = 0xE;  // AStarNode (cost: unsigned short, 0.25 m)
constexpr float kMetricScale = 0.25f, kCostCap = 3000.0f;

constexpr float kWrongWayWeight = 20.0f;  // a segment run against the race's direction costs this many times its length extra

float g_length_bits_scratch = 0.0f, g_extra_scratch = 0.0f;
alignas(16) unsigned char g_fpu_state[112];  // the x87 state saved by fnsave (108 bytes) while our function runs

struct V2 {
    float x, z;
};

V2 direction(const float *from, const float *to) {
    const float dx = to[0] - from[0], dz = to[2] - from[2];
    const float l = std::hypot(dx, dz);
    return l > 1e-3f ? V2{dx / l, dz / l} : V2{0, 0};
}

// What a car does along one segment: the seconds it takes (with the time braking adds) and the speed it leaves at.
struct Drive {
    float seconds, exit_speed;
};

Drive drive_segment(const car_stats::Stats &car, float entry_speed, float limit, float length) {
    const float top = std::max(1.0f, car.top_speed), cap = std::max(2.0f, std::min(limit, top));
    float v = std::max(0.0f, entry_speed), seconds = 0.0f;
    if (v > cap) {  // slowing down to the segment's limit: the braking takes longer than the same distance at the entry speed
        const float brake_seconds = (v - cap) / std::max(1.0f, car.brake), brake_distance = 0.5f * (v + cap) * brake_seconds;
        seconds += brake_seconds - brake_distance / v;
        v = cap;
    }
    constexpr float kStepLength = 3.0f;
    for (float done = 0.0f; done < length - 1e-3f;) {
        if (v >= cap) {  // at the limit it stays there: the rest of the segment in one go
            seconds += (length - done) / std::max(2.0f, cap);
            break;
        }
        const float ds = std::min(kStepLength, length - done);
        const float a = car_stats::acceleration(car, v);
        const float next = std::min(cap, std::sqrt(v * v + 2.0f * a * ds));
        seconds += ds / std::max(2.0f, 0.5f * (v + next));
        v = next;
        done += ds;
    }
    return {seconds, v};
}

// The state of a step of the search that has been costed: the cost it had and the speed the car leaves it at, by (road node, segment).
struct State {
    float total, speed;
};
std::unordered_map<unsigned, State> g_states;
const void *g_search = nullptr, *g_logged_search = nullptr;
DWORD g_last_step = 0, g_last_logged = 0;

unsigned state_key(unsigned node, unsigned segment) { return (node << 16) | (segment & 0xFFFF); }

// The extra cost, in metres, of the step from `node` (an AStarNode) along `segment`, whose length is `length`. Nothing for a search that is not an improved car's.
unsigned __cdecl step_extra_bits(const unsigned char *search, const unsigned char *node, const unsigned char *segment, unsigned length_bits) {
    float extra = 0.0f;
    float velocity[3] = {0, 0, 0};
    float length;
    memcpy(&length, &length_bits, 4);
    // search, node and segment are the game's own (valid); no IsBadReadPtr here, it is slow and this runs for every step of the search.
    const void *nav = *reinterpret_cast<void *const *>(search + kSearchNav);
    int player = nav ? control::improved_player(nav) : -1;
    if (player >= 0 && !control::car_velocity(player, velocity)) player = -1;
    if (player >= 0) {
        const unsigned here = *reinterpret_cast<const short *>(node + kNodeRoadNode);
        const unsigned incoming_index = *reinterpret_cast<const unsigned short *>(node + kNodeSegment);
        const unsigned char *here_node = road::node(here), *incoming = road::segment(incoming_index);
        const unsigned next = road::other_node(segment, here);
        const float *p_here = road::node_position(here), *p_next = road::node_position(next);
        const unsigned index = static_cast<unsigned>((segment - *reinterpret_cast<const unsigned char *const *>(road::kSegments)) / road::kSegmentSize);
        if (here_node && incoming && p_here && p_next) {
            geometry::precompute_limits();
            const car_stats::Stats &car = car_stats::of(player);
            const float speed = std::hypot(velocity[0], velocity[2]);
            const bool seed = *reinterpret_cast<const short *>(node + kNodeParentSlot) < 0;  // the first steps of the search: the car is on the way into them
            const DWORD now = GetTickCount();
            if (search != g_search || now - g_last_step > 300) {  // a new search
                g_states.clear();
                g_search = search;
                if (g_debug_log) {
                    static int searches = 0;
                    SYSTEMTIME t;
                    GetLocalTime(&t);
                    g_log.line("ai search #%d %02d:%02d:%02d: starts at junction %u (%.0f %.0f), car speed %.0f m/s", ++searches, t.wHour, t.wMinute, t.wSecond, here, p_here[0], p_here[2], speed);
                }
            }
            g_last_step = now;

            // 1. the time: the speed the car arrives at, the segment's limit, the length
            const float travelled = *reinterpret_cast<const unsigned short *>(node + kNodeCost) * kMetricScale;
            float entry = speed;
            if (!seed) {
                const auto found = g_states.find(state_key(here, incoming_index));
                if (found != g_states.end()) entry = found->second.speed;
            }
            const float limit_speed = car_stats::cornering_speed(car, geometry::corner_limit(index).radius);
            const Drive drive = drive_segment(car, entry, limit_speed, length);
            extra = std::max(0.0f, drive.seconds * car.top_speed - length);
            const float time_extra = extra;
            float wrong_way_extra = 0.0f;

            // 2. against the race's direction (only for a car that is not held to the race route by the game's race filter)
            if (!settings::player(player).follow_race_route) {
                float rx, rz;
                if (race_area::route_direction(index, &rx, &rz)) {
                    const V2 travel = direction(p_here, p_next);
                    if (travel.x * rx + travel.z * rz < -0.5f) {
                        wrong_way_extra = length * kWrongWayWeight;
                        extra += wrong_way_extra;
                    }
                }
            }
            extra = std::fmin(extra, kCostCap);

            // The state the step leaves, for the steps after it (the cheaper way to a (node, segment) is the one that counts)
            const float total = travelled + length + extra;
            State &state = g_states[state_key(next, index)];
            if (state.total == 0.0f || total < state.total) state = {total, drive.exit_speed};
            if (g_debug_log) {  // the first steps of each search, and every step the wrong-way cost was added to
                static int in_search = 0, wrong_way_lines = 0;
                if (search != g_logged_search || now - g_last_logged > 300) {
                    g_logged_search = search;
                    in_search = 0;
                }
                g_last_logged = now;
                if (in_search++ < 150 || (wrong_way_extra > 0.0f && wrong_way_lines++ < 300))
                    g_log.line("ai path cost: segment %u (%.0f m, limit %.0f m/s) entered at %.0f m/s, left at %.0f m/s: %.1f s = +%.0f m%s; node %u to %u, total so far %.0f m", index, length, limit_speed, entry,
                               drive.exit_speed, drive.seconds, time_extra, wrong_way_extra > 0.0f ? " AND THE WRONG WAY COST" : "", here, next, total);
            }
        }
        // Shortcuts set to Always: a shortcut segment costs a tenth of its length, so the search takes it whenever it leads the right way.
        if (settings::player(player).shortcuts == settings::kAlways && (road::segment_flags(segment) & road::kFlagShortcut)) extra = -0.9f * length;
    }
    unsigned bits;
    memcpy(&bits, &extra, 4);
    return bits;
}

// WRoadNav::MakeShortcutDecision 0x76FF00 (this, shortcut number, cached, allowed), ret 0xC. For a human-class car's nav (what the AI drives as) the
// game only allows the shortcut whose number the nav itself is on or at the entrance of, so the route search never plans a shortcut from afar. For
// a improved car every shortcut is allowed and the cost decides, like for any other road.
using ShortcutFn = bool(FASTCALL *)(void *, void *, int, unsigned *, unsigned *);
ShortcutFn g_orig_shortcut = nullptr;

bool FASTCALL hook_shortcut(void *nav, void *edx, int number, unsigned *cached, unsigned *allowed) {
    if (number != 0xFF) {
        const int player = control::improved_player(nav);
        if (player >= 0 && settings::player(player).shortcuts != settings::kAvoid) return true;
    }
    return g_orig_shortcut(nav, edx, number, cached, allowed);
}

// AStarSearch::Admissible 0x773B60 (this = the search, the segment, whether it is driven forward, the EPathType), ret 0xC. What a search may use; for a
// human-class car's nav (path type 4, what the AI drives as) a shortcut segment is accepted only when the nav's own shortcut number equals the segment's
// (compared in the function itself: it does not call MakeShortcutDecision for this type, only for a racer, type 2). For an improved car's nav the
// shortcut setting decides: Avoid, a shortcut is never admissible; Allowed and Always, the racer's rule is used instead, whose shortcut check is the
// MakeShortcutDecision above (yes), with the same rules for one-way roads and barriers.
using AdmissibleFn = bool(__fastcall *)(void *, void *, const unsigned char *, bool, int);
AdmissibleFn g_orig_admissible = nullptr;

bool __fastcall hook_admissible(void *search, void *edx, const unsigned char *segment, bool forward, int path_type) {
    if (path_type == kPathPlayer && segment && (road::segment_flags(segment) & road::kFlagShortcut)) {
        void *nav = *reinterpret_cast<void **>(static_cast<unsigned char *>(search) + kSearchNav);
        const int player = nav ? control::improved_player(nav) : -1;
        if (player >= 0) {
            static int logged = 0;
            if (settings::player(player).shortcuts == settings::kAvoid) return false;
            const bool ok = g_orig_admissible(search, edx, segment, forward, kPathRacer);
            if (logged < 8 && ok) g_log.line("ai shortcuts: a shortcut segment is admissible for player %d's route search (%d so far)", player + 1, ++logged);
            return ok;
        }
    }
    return g_orig_admissible(search, edx, segment, forward, path_type);
}

}  // namespace

void install() {
    {
        const nfsmw::Hook hooks[] = {
            {"ai_shortcuts", "WRoadNav::MakeShortcutDecision", 0x76FF00, {0x51, 0x53, 0x55, 0x56, 0x8B, 0x74, 0x24}, false, nfsmw::detour(&hook_shortcut),
             nfsmw::orig(&g_orig_shortcut)},
            {"ai_admissible", "AStarSearch::Admissible", 0x773B60, {0x8B, 0x44, 0x24, 0x0C, 0x83, 0xF8, 0x06}, false, nfsmw::detour(&hook_admissible),
             nfsmw::orig(&g_orig_admissible)},
        };
        nfsmw::install_hooks(hooks, std::size(hooks), g_log, nullptr, "ai shortcuts");
    }

    // The stub at the fmul that makes the step's length (0x788206: fmul dword ptr [0x9B3A14], with the search in edi, the AStarNode in ebx and the
    // WRoadSegment in ebp): the same fmul, the length stored, our function called with (search, node, segment, length bits) and its extra added.
    // The game's code has values on the x87 stack at this point (the length, and more), and our function does its float arithmetic on that stack too:
    // with fewer than eight free registers that overflows the stack and the arithmetic turns into NaN, which the game then stores as a cost of 0
    // (the search ran greedily, every step's cost without the cost of the way there). So the whole x87 state is saved (fnsave, which also empties the
    // stack) around the call and put back (frstor) after it.
    nfsmw::Stub stub(160);
    stub.emit({0xD8, 0x0D});
    stub.emit_u32(0x9B3A14);  // fmul dword ptr [0x9B3A14]
    stub.emit({0xD9, 0x15});
    stub.emit_u32(reinterpret_cast<uintptr_t>(&g_length_bits_scratch));  // fst dword ptr [length]
    stub.emit({0xDD, 0x35});
    stub.emit_u32(reinterpret_cast<uintptr_t>(g_fpu_state));  // fnsave [state]: the x87 state is kept there, the stack is empty
    stub.emit({0x60, 0xFF, 0x35});                                          // pushad; push dword ptr [length]
    stub.emit_u32(reinterpret_cast<uintptr_t>(&g_length_bits_scratch));
    stub.emit({0x55, 0x53, 0x57, 0xB8});  // push ebp; push ebx; push edi; mov eax,
    stub.emit_u32(reinterpret_cast<uintptr_t>(&step_extra_bits));
    stub.emit({0xFF, 0xD0, 0x83, 0xC4, 0x10, 0xA3});  // call eax; add esp,16; mov [extra], eax
    stub.emit_u32(reinterpret_cast<uintptr_t>(&g_extra_scratch));
    stub.emit({0x61, 0xDD, 0x25});
    stub.emit_u32(reinterpret_cast<uintptr_t>(g_fpu_state));  // popad; frstor [state]: the game's x87 stack is back, the length in st0
    stub.emit({0xD8, 0x05});
    stub.emit_u32(reinterpret_cast<uintptr_t>(&g_extra_scratch));  // fadd dword ptr [extra]
    stub.emit_rel32(0xE9, 0x78820C);
    stub.done();
    const unsigned char expected[6] = {0xD8, 0x0D, 0x14, 0x3A, 0x9B, 0x00};
    g_log.line("ai path cost: the route search of the improved AI costs time %s (wrong way %.0f)",
               nfsmw::jump_to_stub(0x788206, expected, 6, stub) ? "enabled" : "NOT patched (the code differs)", kWrongWayWeight);
}

}  // namespace improved_ai_opponents::path_cost

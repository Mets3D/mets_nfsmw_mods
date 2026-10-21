// The game's route search (AStarSearch::Service 0x788040) costs a step along a road segment as its length: it takes the shortest
// way, whatever the road is like and whatever the car can do. For the cars whose AI is "improved" (ai_control.cpp) a step costs more:
//  - Width, relative to the alternatives. At a junction the roads that lead on from it are compared: a road narrower than the widest of them
//    costs its length times (1 + width_weight * (1 - width / widest width)). A road that is the only way on, or as wide as its widest
//    sibling, is not penalised: what counts as narrow is whatever the other choices are.
//  - What the car can do. The direction the route takes at a junction against the direction it arrives in is a turn; over the distance
//    between the junction's neighbours it needs a curvature (angle / distance). The car, at the speed it would still have there (it brakes
//    as the route gets longer: brake_accel), can only do lateral_accel / speed^2. When the route asks for more, the step costs
//    kinematic_weight metres for every multiple of the possible curvature it asks for above 1, up to a cap. The first junction of the search
//    is entered at the car's own heading (its velocity), so a road that leaves at 70 degrees 10 m ahead of a car doing 200 km/h costs the cap.
// The step's cost is the game's `actual_cost = parent_cost + length` (the fmul at 0x788206 is where the length is made): a stub there adds our
// extra to the length. The search's heuristic (the straight distance to the goal) is not touched: costs only go up, so it still holds.
// The search is for one nav: the extra is only added when it is the drive-to nav of a car set to "improved"; any other search (the
// vanilla AI's, the opponents', the cops') gets nothing added.
#include "path_cost.h"

#include <algorithm>
#include <cstring>

#include "ai_control.h"
#include "hooks.h"
#include "patch.h"
#include "road_network.h"
#include "settings.h"
#include "improved_ai_opponents.h"

namespace improved_ai_opponents::path_cost {

namespace {

constexpr unsigned kSearchNav = 0x2C;                                  // AStarSearch: the WRoadNav it is for
constexpr unsigned kNodeParentSlot = 0x8, kNodeSegment = 0xA, kNodeRoadNode = 0xC, kNodeCost = 0xE;  // AStarNode (cost: unsigned short, 0.25 m)
constexpr float kMetricScale = 0.25f, kCostCap = 3000.0f;

// [improved_ai] settings (integers; the percent ones are hundredths).
float g_width_weight = 1.0f;      // [improved_ai] width_weight (percent): extra cost of the narrowest road relative to the widest sibling
float g_lateral_accel = 10.0f;    // [improved_ai] lateral_accel (m/s^2): what the car can corner at
float g_brake_accel = 12.0f;      // [improved_ai] brake_accel (m/s^2): how much speed it can lose along the route
float g_kinematic_weight = 30.0f; // [improved_ai] kinematic_weight (m): cost of asking twice the possible curvature

float g_length_bits_scratch = 0.0f, g_extra_scratch = 0.0f;

struct V2 {
    float x, z;
};

V2 direction(const float *from, const float *to, float *length) {
    const float dx = to[0] - from[0], dz = to[2] - from[2];
    const float l = std::hypot(dx, dz);
    if (length) *length = l;
    return l > 1e-3f ? V2{dx / l, dz / l} : V2{0, 0};
}

// The turn (radians) from direction `a` to `b`.
float turn_angle(const V2 &a, const V2 &b) {
    const float c = std::fmax(-1.0f, std::fmin(1.0f, a.x * b.x + a.z * b.z));
    return std::acos(c);
}

// The extra cost, in metres, of the step from `node` (an AStarNode) along `segment`, whose length is `length`. Nothing for a search that is not
// a improved car's.
unsigned __cdecl step_extra_bits(const unsigned char *search, const unsigned char *node, const unsigned char *segment, unsigned length_bits) {
    float extra = 0.0f;
    float velocity[3] = {0, 0, 0};
    float length;
    memcpy(&length, &length_bits, 4);
    // search, node and segment are the game's own (valid); no IsBadReadPtr here, it is slow and this runs for every step of the search.
    const void *nav = *reinterpret_cast<void *const *>(search + kSearchNav);
    const int player = nav ? control::improved_player(nav, velocity) : -1;
    if (player >= 0) {
        const unsigned here = *reinterpret_cast<const short *>(node + kNodeRoadNode);
        const unsigned incoming_index = *reinterpret_cast<const unsigned short *>(node + kNodeSegment);
        const unsigned char *here_node = road::node(here), *incoming = road::segment(incoming_index);
        const unsigned a = road::segment_node(segment, 0), b = road::segment_node(segment, 1);
        const unsigned next = a == here ? b : a;
        const float *p_here = road::node_position(here), *p_next = road::node_position(next);
        if (here_node && incoming && p_here && p_next) {
            // 1. width against the widest road that leads on from this junction.
            float widest = 0.0f;
            for (int i = 0; i < road::node_segment_count(here_node) && i < 7; ++i) {
                const unsigned index = road::node_segment(here_node, i);
                if (index == incoming_index) continue;
                const unsigned char *sibling = road::segment(index);
                if (sibling) widest = std::fmax(widest, road::segment_width(sibling));
            }
            const float width = road::segment_width(segment);
            if (widest > 1.0f && width < widest) extra += length * g_width_weight * (1.0f - width / widest);

            // 2. what the car can do: the turn here, over the distance it has.
            const unsigned prev = road::segment_node(incoming, 0) == here ? road::segment_node(incoming, 1) : road::segment_node(incoming, 0);
            const float *p_prev = road::node_position(prev);
            const float speed = std::hypot(velocity[0], velocity[2]);
            const bool seed = *reinterpret_cast<const short *>(node + kNodeParentSlot) < 0;  // the first junctions of the search: the car is on the way into them
            if (g_debug_log) {  // a search is a run of steps close in time: one line for its start, to see when the route is made again and from where
                static DWORD last_step = 0;
                static int steps = 0, searches = 0;
                const DWORD now = GetTickCount();
                if (now - last_step > 300) {
                    SYSTEMTIME t;
                    GetLocalTime(&t);
                    g_log.line("ai search #%d %02d:%02d:%02d: starts at junction %u (%.0f %.0f), car speed %.0f m/s; the last one took %d steps", ++searches, t.wHour, t.wMinute,
                               t.wSecond, here, p_here[0], p_here[2], std::hypot(velocity[0], velocity[2]), steps);
                    steps = 0;
                }
                last_step = now;
                ++steps;
            }
            float l_in = 0, l_out = 0;
            V2 d_out = direction(p_here, p_next, &l_out), d_in{0, 0};
            if (seed && speed > 3.0f) {
                d_in = {velocity[0] / speed, velocity[2] / speed};  // the car's own heading
                l_in = 0.0f;
            } else if (p_prev) {
                d_in = direction(p_prev, p_here, &l_in);
            }
            if (speed > 3.0f && (d_in.x != 0.0f || d_in.z != 0.0f) && (d_out.x != 0.0f || d_out.z != 0.0f)) {
                const float angle = turn_angle(d_in, d_out);
                const float room = std::fmax(1.0f, seed ? l_out : 0.5f * (l_in + l_out));  // the distance the turn has
                const float curvature = angle / room;
                const float travelled = *reinterpret_cast<const unsigned short *>(node + kNodeCost) * kMetricScale;
                const float v2 = std::fmax(15.0f * 15.0f, speed * speed - 2.0f * g_brake_accel * travelled);  // the speed it can have lost by then
                const float possible = g_lateral_accel / v2;
                if (curvature > possible) extra += std::fmin(kCostCap, g_kinematic_weight * (curvature / possible - 1.0f));
            }
        }
        extra = std::fmin(extra, kCostCap);
        // Shortcuts set to Always: a shortcut segment costs a tenth of its length, so the search takes it whenever it leads the right way.
        if (settings::player(player).shortcuts == settings::kAlways && (road::segment_flags(segment) & 0x80)) extra = -0.9f * length;
        if (g_debug_log && extra > 1.0f) {
            static int lines = 0;
            if (lines++ < 60) g_log.line("ai path cost: +%.0f m on a step of %.0f m", extra, length);
        }
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
    float velocity[3];
    if (number != 0xFF) {
        const int player = control::improved_player(nav, velocity);
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
constexpr int kPathRacer = 2, kPathPlayer = 4;

bool __fastcall hook_admissible(void *search, void *edx, const unsigned char *segment, bool forward, int path_type) {
    if (path_type == kPathPlayer && segment && (road::segment_flags(segment) & 0x80)) {
        float velocity[3];
        void *nav = *reinterpret_cast<void **>(static_cast<unsigned char *>(search) + kSearchNav);
        const int player = nav ? control::improved_player(nav, velocity) : -1;
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
    g_width_weight = GetPrivateProfileIntA("improved_ai", "width_weight", 100, g_ini) / 100.0f;
    g_lateral_accel = static_cast<float>(GetPrivateProfileIntA("improved_ai", "lateral_accel", 10, g_ini));
    g_brake_accel = static_cast<float>(GetPrivateProfileIntA("improved_ai", "brake_accel", 12, g_ini));
    g_kinematic_weight = static_cast<float>(GetPrivateProfileIntA("improved_ai", "kinematic_weight", 30, g_ini));
    if (g_lateral_accel < 1.0f) g_lateral_accel = 1.0f;
    {
        const nfsmw::Hook hooks[] = {
            {"ai_shortcuts", "WRoadNav::MakeShortcutDecision", 0x76FF00, {0x51, 0x53, 0x55, 0x56, 0x8B, 0x74, 0x24}, false, nfsmw::detour(&hook_shortcut),
             nfsmw::orig(&g_orig_shortcut)},
            {"ai_admissible", "AStarSearch::Admissible", 0x773B60, {0x8B, 0x44, 0x24, 0x0C, 0x83, 0xF8, 0x06}, false, nfsmw::detour(&hook_admissible),
             nfsmw::orig(&g_orig_admissible)},
        };
        nfsmw::install_hooks(hooks, 2, g_log, nullptr, "ai shortcuts");
    }

    // The stub at the fmul that makes the step's length (0x788206: fmul dword ptr [0x9B3A14], with the search in edi, the AStarNode in ebx and the
    // WRoadSegment in ebp): the same fmul, the length stored, our function called with (search, node, segment, length bits) and its extra added.
    nfsmw::Stub stub(128);
    stub.emit({0xD8, 0x0D});
    stub.emit_u32(0x9B3A14);  // fmul dword ptr [0x9B3A14]
    stub.emit({0xD9, 0x15});
    stub.emit_u32(reinterpret_cast<uintptr_t>(&g_length_bits_scratch));  // fst dword ptr [length]
    stub.emit({0x60, 0xFF, 0x35});                                          // pushad; push dword ptr [length]
    stub.emit_u32(reinterpret_cast<uintptr_t>(&g_length_bits_scratch));
    stub.emit({0x55, 0x53, 0x57, 0xB8});  // push ebp; push ebx; push edi; mov eax,
    stub.emit_u32(reinterpret_cast<uintptr_t>(&step_extra_bits));
    stub.emit({0xFF, 0xD0, 0x83, 0xC4, 0x10, 0xA3});  // call eax; add esp,16; mov [extra], eax
    stub.emit_u32(reinterpret_cast<uintptr_t>(&g_extra_scratch));
    stub.emit({0x61, 0xD8, 0x05});  // popad; fadd dword ptr [extra]
    stub.emit_u32(reinterpret_cast<uintptr_t>(&g_extra_scratch));
    stub.emit_rel32(0xE9, 0x78820C);
    stub.done();
    const unsigned char expected[6] = {0xD8, 0x0D, 0x14, 0x3A, 0x9B, 0x00};
    g_log.line("ai path cost: route search weighs width and what the car can do for the improved AI %s (width weight %.2f, lateral %.0f, brake %.0f, kinematic %.0f)",
               nfsmw::jump_to_stub(0x788206, expected, 6, stub) ? "enabled" : "NOT patched (the code differs)", g_width_weight, g_lateral_accel, g_brake_accel,
               g_kinematic_weight);
}

}  // namespace improved_ai_opponents::path_cost

/*
 * WRoadNav::FindClosestSegmentInd 0x785A00 (this = the nav, point, direction, direction weight, closest point out, time out; ret 0x14) is where a nav is put on a road
 * segment when it is (re)initialised at a point (InitAtPoint: when the AI starts, after a reset, when the car has left its route): the segment whose road is
 * nearest to the point (a point inside the road's width counts as distance 0), plus the direction weight times how far the segment's direction is from the one asked
 * for, over the segments of the grid cells within 32 m. Nothing tells it which of two roads that lie side by side (separated by a wall, a barrier, a drop) the car
 * is on: wide roads that overlap each other tie at distance 0, and the direction decides.
 * For the nav of an improved AI's car (a point within a few metres of that car) the answer is kept only if its segment is within kMaxHops
 * segments of the segment the car is on (the one whose road is nearest to the car, in 3D, by the metres between the car and the road's edge) by the road
 * network's own connections (a hop: from a segment to another that shares a node). Otherwise the best segment within that many hops is used, scored as the game
 * scores them (the race filter of the nav is respected). A segment outside the race (race_area.cpp) is never kept: the roads beyond the track's barriers.
 */
#include "nav_landing.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#include "ai_control.h"
#include "hooks.h"
#include "improved_ai_opponents.h"
#include "race_area.h"
#include "road_geometry.h"

namespace improved_ai_opponents::nav_landing {

namespace {

using FindFn = int(__fastcall *)(void *, void *, const float *, const float *, float, float *, float *);
FindFn g_orig_find = nullptr;

constexpr unsigned kNavRaceFilter = 0x51;  // WRoadNav::bRaceFilter
constexpr float kCarRadius = 6.0f;         // a point this near an improved car's position is that car's
constexpr float kLookRange = 80.0f;        // segments farther than this from the point (chord, 3D) are not looked at

constexpr int kMaxHops = 4;  // how many connections from the car's own segment a nav may be on

// How far the point is outside the road of the segment (metres, 0 inside it: the road's width is the drivable width of its nodes, interpolated), and where on it.
struct RoadDistance {
    float outside = 1e30f;
    geometry::Nearest nearest{};
};

bool road_distance(unsigned index, const float point[3], RoadDistance *out) {
    geometry::SegmentGeometry g;
    if (!geometry::segment_geometry(index, &g)) return false;
    // A quick look at the chord first
    const float mx = 0.5f * (g.node_a.x + g.node_b.x) - point[0], my = 0.5f * (g.node_a.y + g.node_b.y) - point[1], mz = 0.5f * (g.node_a.z + g.node_b.z) - point[2];
    if (std::sqrt(mx * mx + my * my + mz * mz) > kLookRange + 0.5f * g.between) return false;
    const geometry::Nearest n = geometry::nearest_on_curve(g, point);
    const float width = road::drivable_width(g.a) * (1.0f - n.t) + road::drivable_width(g.b) * n.t;
    out->nearest = n;
    out->outside = std::max(0.0f, n.distance - 0.5f * width);
    return true;
}

// The segments within `hops` connections of `from`, each with its number of hops (-1: not reached).
std::vector<short> reach(unsigned from, int hops) {
    std::vector<short> reached(road::segment_count(), -1);
    if (from >= reached.size()) return reached;
    std::vector<unsigned> frontier = {from}, next;
    reached[from] = 0;
    for (int hop = 1; hop <= hops && !frontier.empty(); ++hop) {
        next.clear();
        for (const unsigned segment : frontier) {
            const unsigned char *s = road::segment(segment);
            if (!s) continue;
            for (int end = 0; end < 2; ++end) {
                const unsigned char *node = road::node(road::segment_node(s, end));
                if (!node) continue;
                for (int i = 0; i < std::min(road::node_segment_count(node), 7); ++i) {
                    const unsigned other = road::node_segment(node, i);
                    if (other < reached.size() && reached[other] < 0) {
                        reached[other] = static_cast<short>(hop);
                        next.push_back(other);
                    }
                }
            }
        }
        frontier.swap(next);
    }
    return reached;
}

int __fastcall hook_find(void *self, void *edx, const float *point, const float *dir, float dir_weight, float *closest, float *time) {
    const int result = g_orig_find(self, edx, point, dir, dir_weight, closest, time);
    if (result < 0 || !road::loaded() || !point || control::improved_player_near(point, kCarRadius) < 0) return result;

    // The segment the car is on: the road nearest to it (kept for a moment: the game asks several times from the same place)
    static float cached_point[3] = {1e9f, 1e9f, 1e9f};
    static DWORD cached_time = 0;
    static int base = -1;
    static std::vector<short> reached;
    const DWORD now = GetTickCount();
    const float cx = point[0] - cached_point[0], cy = point[1] - cached_point[1], cz = point[2] - cached_point[2];
    if (now - cached_time > 200 || cx * cx + cy * cy + cz * cz > 4.0f) {
        base = -1;
        float base_outside = 1e30f, base_centre = 1e30f;
        for (unsigned i = 0; i < road::segment_count(); ++i) {
            RoadDistance d;
            if (!road_distance(i, point, &d)) continue;
            if (d.outside < base_outside || (d.outside == base_outside && d.nearest.distance < base_centre)) {
                base = static_cast<int>(i);
                base_outside = d.outside;
                base_centre = d.nearest.distance;
            }
        }
        reached = base < 0 ? std::vector<short>() : reach(static_cast<unsigned>(base), kMaxHops);
        memcpy(cached_point, point, sizeof(cached_point));
        cached_time = now;
    }
    if (base < 0) return result;
    const bool connected = static_cast<unsigned>(result) < reached.size() && reached[result] >= 0;
    const bool inside = race_area::contains_segment(static_cast<unsigned>(result));
    if (connected && inside) return result;  // the game's answer is connected to the car's road and part of the race

    // The best segment within reach, as the game scores them: the distance to the road (0 inside it) plus the direction weight times 10 times how far the segment's
    // direction is from the asked one (not the absolute value on a one-way segment).
    const bool race_filter = *reinterpret_cast<const unsigned char *>(reinterpret_cast<const unsigned char *>(self) + kNavRaceFilter) != 0 && *reinterpret_cast<const unsigned char *>(road::kValidRaceFilter) != 0;
    float length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (length < 1e-4f) length = 1.0f;
    int best = -1;
    float best_score = 1e30f;
    RoadDistance best_distance;
    for (unsigned i = 0; i < reached.size(); ++i) {
        if (reached[i] < 0 || !race_area::contains_segment(i)) continue;
        const unsigned char *s = road::segment(i);
        if (!s || (race_filter && !(road::segment_flags(s) & road::kFlagInRace))) continue;
        RoadDistance d;
        if (!road_distance(i, point, &d)) continue;
        const float *pa = road::node_position(road::segment_node(s, 0)), *pb = road::node_position(road::segment_node(s, 1));
        if (!pa || !pb) continue;
        float sx = pb[0] - pa[0], sy = pb[1] - pa[1], sz = pb[2] - pa[2];
        const float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
        float dot = sl > 1e-4f ? (dir[0] * sx + dir[1] * sy + dir[2] * sz) / (length * sl) : 0.0f;
        if (!(road::segment_flags(s) & road::kFlagOneWay)) dot = std::fabs(dot);
        const float score = d.outside + dir_weight * (1.0f - dot) * 10.0f;
        if (score < best_score) {
            best_score = score;
            best = static_cast<int>(i);
            best_distance = d;
        }
    }
    if (best < 0) return result;
    // Said when the answer changes (the game asks again every frame from the same place)
    static int logged = 0, last_result = -1, last_best = -1, overrides = 0;
    ++overrides;
    if ((result != last_result || best != last_best) && logged++ < 60) {
        g_log.line("nav landing: the game put a nav at %.0f %.0f %.0f on segment %d, which is %s (the road nearest to the car is segment %d, up to %d hops); using segment %d instead (%d overrides so far)",
                   point[0], point[1], point[2], result, !inside ? "outside the race" : "not connected to the car's road", base, kMaxHops, best, overrides);
        last_result = result;
        last_best = best;
    }
    if (closest) {
        closest[0] = best_distance.nearest.position.x;
        closest[1] = best_distance.nearest.position.y;
        closest[2] = best_distance.nearest.position.z;
    }
    if (time) *time = best_distance.nearest.t;
    return best;
}

}  // namespace

void install() {
    const nfsmw::Hook hooks[] = {
        {"nav_landing", "WRoadNav::FindClosestSegmentInd", 0x785A00, {0x6A, 0xFF, 0x68, 0xD6, 0x21, 0x88, 0x00}, false, nfsmw::detour(&hook_find), nfsmw::orig(&g_orig_find)},
    };
    if (nfsmw::install_hooks(hooks, std::size(hooks), g_log, g_ini, "nav landing").ok) g_log.line("nav landing: an improved AI's nav stays within %d segments of the road its car is on", kMaxHops);
}

}  // namespace improved_ai_opponents::nav_landing

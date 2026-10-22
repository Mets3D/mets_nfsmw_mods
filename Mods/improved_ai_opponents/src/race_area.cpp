/*
 * The race's area, as the game's route search sees it. A race flags the segments of its own route (the checkpoints joined by the shortest path: the in-race flag,
 * bit 15, set when the race loads by WRoadNetwork::AddRaceSegments), and the track's barriers flag the segments that cross them (bits 12 and 13, set by
 * ResolveBarriers: the roads that leave the part of the map the race is in); the route search never takes a flagged segment (AStarSearch::Admissible).
 * So the race's area is every segment that can be reached from the route by hops over a shared node without using a barrier-crossing segment; the rest of
 * the map (the roads beyond the barriers) is outside the race. The segments that cross a barrier are outside as well. Worked out when the network or the
 * route changes (checked twice a second). Asked from two threads (the picture and the simulation), so under a lock.
 *
 * The route's direction: a segment of the route has one (bit 2, RaceRouteForward: the route runs from its node 0 to its node 1); any other segment is given the
 * direction of the nearest segment of the route, by the distance from its middle to that segment (3D), when that is within 60 m.
 */
#include "race_area.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "improved_ai_opponents.h"

namespace improved_ai_opponents::race_area {

namespace {

constexpr float kDirectionRange = 60.0f;

Mutex g_lock;

bool g_active = false;
std::vector<float> g_direction;  // per segment: the race's direction along it (x, z), or 0, 0
std::vector<char> g_segment_inside;
const void *g_for_segments = nullptr;
unsigned g_for_count = 0, g_for_signature = 0;
DWORD g_checked = 0;

// Called with the lock held.
void work_out_directions(unsigned count) {
    g_direction.assign(2 * count, 0.0f);
    struct Own {
        float a[3], b[3], x, z;
        unsigned index;
    };
    std::vector<Own> route;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char *s = road::segment(i);
        if (!(road::segment_flags(s) & road::kFlagInRace)) continue;
        const float *pa = road::node_position(road::segment_node(s, 0)), *pb = road::node_position(road::segment_node(s, 1));
        if (!pa || !pb) continue;
        float dx = pb[0] - pa[0], dz = pb[2] - pa[2];
        const float l = std::sqrt(dx * dx + dz * dz);
        if (l < 1.0f) continue;
        const float sign = (road::segment_flags(s) & road::kFlagRaceForward) ? 1.0f : -1.0f;
        route.push_back({{pa[0], pa[1], pa[2]}, {pb[0], pb[1], pb[2]}, sign * dx / l, sign * dz / l, i});
        g_direction[2 * i] = sign * dx / l;
        g_direction[2 * i + 1] = sign * dz / l;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (road::segment_flags(road::segment(i)) & road::kFlagInRace) continue;
        const unsigned char *s = road::segment(i);
        const float *pa = road::node_position(road::segment_node(s, 0)), *pb = road::node_position(road::segment_node(s, 1));
        if (!pa || !pb) continue;
        const float m[3] = {0.5f * (pa[0] + pb[0]), 0.5f * (pa[1] + pb[1]), 0.5f * (pa[2] + pb[2])};
        float best = kDirectionRange;
        const Own *nearest = nullptr;
        for (const Own &r : route) {
            const float ex = r.b[0] - r.a[0], ey = r.b[1] - r.a[1], ez = r.b[2] - r.a[2], l2 = ex * ex + ey * ey + ez * ez;
            float t = l2 > 1e-3f ? ((m[0] - r.a[0]) * ex + (m[1] - r.a[1]) * ey + (m[2] - r.a[2]) * ez) / l2 : 0.0f;
            t = std::max(0.0f, std::min(1.0f, t));
            const float dx = m[0] - (r.a[0] + t * ex), dy = m[1] - (r.a[1] + t * ey), dz = m[2] - (r.a[2] + t * ez);
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d < best) {
                best = d;
                nearest = &r;
            }
        }
        if (nearest) {
            g_direction[2 * i] = nearest->x;
            g_direction[2 * i + 1] = nearest->z;
        }
    }
}

void refresh() {
    const DWORD now = GetTickCount();
    if (g_checked && now - g_checked < 500) return;
    g_checked = now;
    if (!road::loaded()) {
        g_active = false;
        g_segment_inside.clear();
        g_for_segments = nullptr;
        return;
    }
    const unsigned count = road::segment_count();
    const void *segments = *reinterpret_cast<void *const *>(road::kSegments);
    unsigned in_race = 0, signature = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (road::segment_flags(road::segment(i)) & road::kFlagInRace) {
            ++in_race;
            signature ^= (i + 1) * 2654435761u;
        }
    }
    signature += in_race;
    if (segments == g_for_segments && count == g_for_count && signature == g_for_signature) return;
    g_for_segments = segments;
    g_for_count = count;
    g_for_signature = signature;
    g_active = in_race > 0 && *reinterpret_cast<const unsigned char *>(road::kValidRaceFilter) != 0;
    g_segment_inside.assign(count, g_active ? 0 : 1);
    g_direction.clear();
    if (!g_active) return;
    work_out_directions(count);
    std::vector<unsigned> frontier;
    for (unsigned i = 0; i < count; ++i) {
        if (road::segment_flags(road::segment(i)) & road::kFlagInRace) {
            g_segment_inside[i] = 1;
            frontier.push_back(i);
        }
    }
    unsigned inside = static_cast<unsigned>(frontier.size());
    while (!frontier.empty()) {
        const unsigned segment = frontier.back();
        frontier.pop_back();
        const unsigned char *s = road::segment(segment);
        for (int end = 0; end < 2; ++end) {
            const unsigned char *node = road::node(road::segment_node(s, end));
            if (!node) continue;
            for (int i = 0; i < road::node_segment_count(node) && i < 7; ++i) {
                const unsigned other = road::node_segment(node, i);
                const unsigned char *o = road::segment(other);
                if (!o || g_segment_inside[other] || (road::segment_flags(o) & road::kFlagCrossesBarrier)) continue;
                g_segment_inside[other] = 1;
                ++inside;
                frontier.push_back(other);
            }
        }
    }
    g_log.line("race area: %u of %u segments are inside the race (the route has %u)", inside, count, in_race);
}

}  // namespace

bool active() {
    std::lock_guard<Mutex> guard(g_lock);
    refresh();
    return g_active;
}

bool contains_segment(unsigned segment) {
    std::lock_guard<Mutex> guard(g_lock);
    refresh();
    return !g_active || (segment < g_segment_inside.size() && g_segment_inside[segment]);
}

bool contains_node(unsigned node_index) {
    {
        std::lock_guard<Mutex> guard(g_lock);
        refresh();
        if (!g_active) return true;
    }
    const unsigned char *node = road::node(node_index);
    if (!node) return false;
    for (int i = 0; i < road::node_segment_count(node) && i < 7; ++i)
        if (contains_segment(road::node_segment(node, i))) return true;
    return false;
}

bool route_direction(unsigned segment, float *x, float *z) {
    std::lock_guard<Mutex> guard(g_lock);
    refresh();
    if (!g_active || 2 * segment + 1 >= g_direction.size()) return false;
    *x = g_direction[2 * segment];
    *z = g_direction[2 * segment + 1];
    return *x != 0.0f || *z != 0.0f;
}

}  // namespace improved_ai_opponents::race_area

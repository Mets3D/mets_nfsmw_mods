// What the AI of a player's car is calculating (F8), drawn over that player's picture. Nothing is drawn unless F8 (this) or F11 (the road inspector, road_inspector.cpp) is on.
// F8 shows, for each player whose AI is driving the car or planning beside it:
//   the route   the road segments of the route the A* search found to the next checkpoint, from the one the car is on to the goal, each drawn in full: the lanes
//               of its profile and its orange outline (segment_draw). The car's own segment is drawn only from the car on. With [debug] log, every new route
//               (from the segment the car's drive-to nav is on) is also written to the log.
//   yellow      the steering target (IVehicleAI::GetDriveTarget: the point the car steers at) with a pole and a cross, and a line from the car
//   white       the head of the nav (`fPosition`, the look-ahead point the corridor is built up to), a small square with a pole
//   diamond     (the apex) the corner the view along the corridor is blocked by: orange-red when a car's cut is the cause, magenta when it is
//               the road's own edge (a bend); a line from the car to it. The steering target sits beside the apex when there is one.
// Lines are projected with the view's WorldClipMatrix (eViewPlatInfo +0x80, what eViewPlatInterface::GetScreenPosition 0x6BE940 uses; the
// world's axes are swizzled first, x -> -y, z -> x, y -> z) and drawn pre-transformed, with a depth test against the game's depth buffer, so walls hide them.
// Memory is read with checks: a pointer that is not readable ends the drawing, it never crashes the game.
// What the nav, its path and the corridor are and where each value is: agent_docs/ai-driving.md.
#include "overlay.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

#include "ai_control.h"
#include "car_stats.h"
#include "lines.h"
#include "road_geometry.h"
#include "road_inspector.h"
#include "hooks.h"
#include "keys.h"
#include "road_network.h"
#include "segment_draw.h"
#include "improved_ai_opponents.h"

namespace improved_ai_opponents::overlay {

using canvas::Lines;
using geometry::segment_geometry;
using geometry::SegmentGeometry;
using road::readable;

namespace {

constexpr int kMaxPathSegments = 80;
constexpr DWORD kTarget = 0xFFFFE000, kApexCar = 0xFFFF6000, kApexRoad = 0xFFFF00FF, kNavHead = 0xFFFFFFFF;

// WRoadNav, PC offsets.
constexpr unsigned kNavRoadOcclusion = 0x58, kNavAvoidableOcclusion = 0x5C, kNavPosition = 0x98, kNavApex = 0x158;

// A segment of the route: the nodes it is driven from and to.
struct RouteSegment {
    unsigned index = 0, start_node = 0, end_node = 0;
};

// The straight distance in the ground plane between two nodes (0 if one is missing).
float node_distance(unsigned a, unsigned b) {
    const float *pa = road::node_position(a), *pb = road::node_position(b);
    return pa && pb ? std::hypot(pb[0] - pa[0], pb[2] - pa[2]) : 0.0f;
}

// The route of the nav from the segment the car is on to the goal, in travel order. `before`: this many segments of the path that
// come before the nav's own segment are in it too (the nav is the look-ahead target, ahead of the car); `*nav_at` is where the nav's own segment is in the result.
std::vector<RouteSegment> route_of(const unsigned char *nav, int before = 0, size_t *nav_at = nullptr) {
    std::vector<RouteSegment> route;
    if (*reinterpret_cast<const int *>(nav + 0x78) != 3) return route;  // not on a path (no race route yet)
    const int count = *reinterpret_cast<const int *>(nav + 0x2E0);
    const unsigned short *path = *reinterpret_cast<unsigned short *const *>(nav + 0x2E4);
    const unsigned short current = *reinterpret_cast<const unsigned short *>(nav + 0x8E);
    const unsigned short goal = *reinterpret_cast<const unsigned short *>(nav + 0x2DA);
    if (count <= 0 || count > 2000 || !readable(path, count * 2)) return route;
    std::vector<unsigned> ids;
    int first = 0;
    while (first < count && path[first] != current) ++first;
    if (first == count) first = 0;
    const int begin = std::max(0, first - before);
    if (nav_at) *nav_at = static_cast<size_t>(first - begin);
    for (int i = begin; i < count && static_cast<int>(ids.size()) < kMaxPathSegments; ++i) {
        ids.push_back(path[i]);
        if (path[i] == goal) break;
    }
    unsigned previous_end = 0xFFFFFFFF;
    for (size_t i = 0; i < ids.size(); ++i) {
        const unsigned char *s = road::segment(ids[i]);
        if (!s) break;
        const unsigned a = road::segment_node(s, 0), b = road::segment_node(s, 1);
        unsigned start = a, end = b;
        if (previous_end != 0xFFFFFFFF) {
            if (previous_end == b) std::swap(start, end);
        } else if (i + 1 < ids.size()) {  // the first one ends at the node it shares with the next
            const unsigned char *n = road::segment(ids[i + 1]);
            if (n && (a == road::segment_node(n, 0) || a == road::segment_node(n, 1))) std::swap(start, end);
        }
        previous_end = end;
        if (!road::node_position(start) || !road::node_position(end)) continue;
        route.push_back({ids[i], start, end});
    }
    return route;
}


// A line of the log for every segment the first time any route has it ([debug] log; to find what makes a road bad; a lap's worth of segments is a few hundred).
void log_route(const std::vector<RouteSegment> &route) {
    static std::unordered_set<unsigned> seen;
    for (size_t i = 0; i < route.size(); ++i) {
        const RouteSegment &r = route[i];
        if (seen.size() > 1500 || !seen.insert(r.index).second) continue;
        const unsigned char *s = road::segment(r.index);
        const float *from = road::node_position(r.start_node);
        const unsigned road_index = *reinterpret_cast<const short *>(s + 6);
        const unsigned char *road_row = *reinterpret_cast<unsigned char *const *>(road::kRoads) + road_index * road::kRoadSize;
        float turn_deg = 0.0f;  // how far the route turns at the end of this segment into the next
        if (i + 1 < route.size()) {
            const float *a0 = road::node_position(r.start_node), *a1 = road::node_position(r.end_node);
            const float *b0 = road::node_position(route[i + 1].start_node), *b1 = road::node_position(route[i + 1].end_node);
            const float ax = a1[0] - a0[0], az = a1[2] - a0[2], bx = b1[0] - b0[0], bz = b1[2] - b0[2];
            const float la = std::hypot(ax, az), lb = std::hypot(bx, bz);
            if (la >= 0.01f && lb >= 0.01f) turn_deg = std::acos(std::clamp((ax * bx + az * bz) / (la * lb), -1.0f, 1.0f)) * 57.29578f;
        }
        char lanes[160];
        const float width = std::fmin(road::drivable_width(r.start_node, lanes, sizeof(lanes)), road::drivable_width(r.end_node));
        g_log.line("ai route seg %u road %d at %.0f %.0f: length %.0f, drivable width %.1f, road min width %u, flags %#x, turn %.0f deg, lanes(type:width) %s", r.index, static_cast<short>(road_index),
                   from[0], from[2], node_distance(r.start_node, r.end_node), width, readable(road_row, road::kRoadSize) ? road_row[5] : 0u, road::segment_flags(s), turn_deg, lanes);
    }
}

// F8 turns the view of what the AI is calculating on and off (see the top of the file).
bool g_ai_view = false;

// Every new route is written to the log (what the route is, segment by segment, with the length, the speed a car can follow it at and the time that takes at that
// speed: not a lap time, a way to compare routes). Every different route once; at most 400.
void log_route_summary(const std::vector<RouteSegment> &route) {
    static std::vector<unsigned> seen;
    static int logged = 0;
    unsigned signature = static_cast<unsigned>(route.size());
    for (const RouteSegment &r : route) signature = signature * 31u + r.index + 1u;
    if (route.empty() || logged >= 400 || std::find(seen.begin(), seen.end(), signature) != seen.end()) return;
    seen.push_back(signature);
    ++logged;
    float length = 0.0f, seconds = 0.0f;
    std::string text;
    int on_line = 0;
    g_log.line("route #%d: %zu segments from segment %u", logged, route.size(), route.front().index);
    for (const RouteSegment &r : route) {
        const float limit = car_stats::cornering_speed(car_stats::of(-1), geometry::corner_limit(r.index).radius);
        const float segment_length = node_distance(r.start_node, r.end_node);
        length += segment_length;
        seconds += segment_length / std::max(5.0f, limit);
        char item[80];
        snprintf(item, sizeof(item), " %u(%.0fm,%.0f)", r.index, segment_length, limit);
        text += item;
        if (++on_line == 10) {
            g_log.line("route #%d:%s", logged, text.c_str());
            text.clear();
            on_line = 0;
        }
    }
    if (!text.empty()) g_log.line("route #%d:%s", logged, text.c_str());
    g_log.line("route #%d: %.0f m between the nodes, %.1f s at the segments' limit speeds (segment(length, limit m/s))", logged, length, seconds);
}

// The route from the segment the car is on: that segment is the one of the path (up to 12 before the nav's, which is the look-ahead target) whose road is nearest
// to the car, and it is drawn from the car on, without the part behind it.
void add_route_segments(Lines &lines, const unsigned char *nav, const float *car) {
    constexpr DWORD kOutline = 0xFFFFA030;
    constexpr int kBefore = 12;
    size_t nav_at = 0;
    const std::vector<RouteSegment> route = route_of(nav, kBefore, &nav_at);
    if (route.empty()) return;
    const std::vector<RouteSegment> ahead(route.begin() + static_cast<std::ptrdiff_t>(nav_at), route.end());
    log_route_summary(ahead);
    if (g_debug_log) log_route(ahead);

    size_t first = nav_at;
    float t_car = -1.0f;
    if (car) {
        float best = 1e9f;
        for (size_t i = 0; i <= nav_at; ++i) {
            SegmentGeometry g;
            if (!segment_geometry(route[i].index, &g)) continue;
            const geometry::Nearest nearest = geometry::nearest_on_curve(g, car);
            if (nearest.distance <= best) {
                best = nearest.distance;
                first = i;
                t_car = nearest.t;
            }
        }
    }
    for (size_t i = first; i < route.size(); ++i) {
        const RouteSegment &r = route[i];
        float t_from = 0.0f, t_to = 1.0f;
        if (i == first && t_car >= 0.0f) {
            SegmentGeometry g;
            if (segment_geometry(r.index, &g)) (r.start_node == g.a ? t_from : t_to) = t_car;  // the route runs from a to b, or back
        }
        segment_draw::add_lanes(lines, r.index, t_from, t_to);
        segment_draw::add_corridor(lines, r.index, kOutline, 0, t_from, t_to);
    }
}

}  // namespace

void draw(IDirect3DDevice9 *device, const void *view_plat_info, float width, float height, int player) {
    static bool f8_was_down = false;
    if (nfsmw::key_pressed(nfsmw::game_has_focus() && nfsmw::key_down(VK_F8), f8_was_down)) g_ai_view = !g_ai_view;
    const bool editing = road_inspector::enabled();
    if (!view_plat_info || (!g_ai_view && !editing)) return;  // neither F8 nor F11: nothing is drawn
    const float *world_clip = reinterpret_cast<const float *>(static_cast<const unsigned char *>(view_plat_info) + 0x80);
    if (!readable(world_clip, 64)) return;

    Lines lines(world_clip, width, height);
    road_inspector::begin_picture();
    if (editing) road_inspector::add(lines);
    void *ai = g_ai_view ? control::driving_ai(player) : nullptr;
    if (!ai) {
        if (!lines.empty()) draw_lines(device, lines);
        return;
    }
    void *nav = control::drive_to_nav(ai);
    const unsigned char *n = readable(nav, 0x300) ? static_cast<const unsigned char *>(nav) : nullptr;

    using GetVecFn = const float *(__thiscall *)(void *);
    const float *target = reinterpret_cast<GetVecFn>((*static_cast<void ***>(ai))[0x44 / 4])(ai);  // IVehicleAI::GetDriveTarget
    const float *car = control::car_position(ai);
    const bool have_car = readable(car, 12);

    if (n) {
        add_route_segments(lines, n, have_car ? car : nullptr);

        // The apex: the corner the view along the corridor is blocked by (when there is one), and why.
        const int by_road = *reinterpret_cast<const int *>(n + kNavRoadOcclusion), by_car = *reinterpret_cast<const int *>(n + kNavAvoidableOcclusion);
        if (by_road || by_car) {
            const float *apex = reinterpret_cast<const float *>(n + kNavApex);
            const DWORD color = by_car ? kApexCar : kApexRoad;
            const Vec3 a = {apex[0], apex[1], apex[2]};
            add_diamond(lines, a, 1.2f, color);
            add_diamond(lines, {a.x, a.y + 0.5f, a.z}, 0.8f, color);
            add_pole(lines, a, 6.0f, color);
            if (have_car) lines.add({car[0], car[1] + 0.6f, car[2]}, a, color);
        }
        // The nav's head.
        const float *head = reinterpret_cast<const float *>(n + kNavPosition);
        add_square(lines, {head[0], head[1] + 0.3f, head[2]}, 0.7f, kNavHead);
        add_pole(lines, {head[0], head[1], head[2]}, 3.0f, kNavHead);
    }
    if (readable(target, 12)) {
        const Vec3 t = {target[0], target[1], target[2]};
        add_pole(lines, t, 8.0f, kTarget);
        add_cross(lines, {t.x, t.y + 0.3f, t.z}, 1.5f, kTarget);
        if (have_car) lines.add({car[0], car[1] + 0.6f, car[2]}, {t.x, t.y + 0.6f, t.z}, kTarget);
    }
    draw_lines(device, lines);
}

}  // namespace improved_ai_opponents::overlay

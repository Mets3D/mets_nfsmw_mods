// What the AI of a player's car is calculating (the AI Logic overlay), drawn over that player's picture. F8 steps each player's overlay through Off, AI Logic,
// Roads and Cost (settings.h; the last two are road_inspector.cpp's). AI Logic shows, for each player whose AI is driving the car or planning beside it:
//   the route   the road segments of the route the A* search found to the next checkpoint, from the one the car is on to the goal, each drawn in full: the lanes
//               of its profile and its orange outline (segment_draw). The car's own segment is drawn only from the car on. With [debug] log, every new route
//               (from the segment the car's drive-to nav is on) is also written to the log.
//   green       the cookie trail: the road's cross-sections between the car and the nav's head, as the two limits and a bar across every fourth one; red where the car is out
//               of bounds by the game's measure (what throws the route away: AIActionRace::CheckOffPath)
//   boxes       (with the Avoid Props option, improved AI) the props around the car (smackables): orange-red the ones the AI steers around, yellow the same knocked down, grey-blue the light ones it drives
//               through, each with its mass in kg (obstacles.cpp)
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
#include "obstacles.h"
#include "road_geometry.h"
#include "road_inspector.h"
#include "settings.h"
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
constexpr unsigned kNavTrail = 0x08, kNavHasTrail = 0x55, kNavRoadOcclusion = 0x58, kNavAvoidableOcclusion = 0x5C, kNavPosition = 0x98, kNavApex = 0x158;

// A NavCookie (0x40 bytes, the decomp's layout is the PC's): a cross-section of the road the nav has walked along; the positions of its edges are (x, z).
struct Cookie {
    float left[2], right[2], forward[2];
    float length, curvature, left_offset, right_offset;
    unsigned flags;
    float pad;
    float centre[3];
    short segment_parameter;
    unsigned short segment;
};
static_assert(sizeof(Cookie) == 0x40, "NavCookie");
constexpr int kTrailSize = 32;
constexpr DWORD kCookieEdge = 0xFF30D030, kCookieBar = 0xFF1C7A1C, kCookieOut = 0xFFFF3030;

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
    const float *goal = road::node_position(route.back().end_node);
    g_log.line("route #%d: %zu segments from segment %u to segment %u, which ends at %.0f %.0f", logged, route.size(), route.front().index, route.back().index, goal ? goal[0] : 0.0f, goal ? goal[2] : 0.0f);
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

// The cookie trail of the nav (agent_docs/ai-driving.md): the road's cross-sections between the car and the nav's head, one every 1 to 3 m, each with the left and
// right limit of the road there. This is the corridor the car steers through (the farthest point it can see through it is its steering target) and what the game
// holds the car against: the cookie nearest to the car gives the car's offset across it, and the game's out-of-bounds value (the car's half width of 1 m plus how far
// the car is outside the limits) is over 2 m, the nav is thrown onto the car's road and the route is dropped (AIActionRace::CheckOffPath). Drawn as the two limits
// (green) and a bar across every fourth cookie; the bar nearest to the car and a line to it are red when the car is out of bounds in that sense.
void add_cookie_trail(Lines &lines, const unsigned char *nav, const float *car) {
    const unsigned char *trail = *reinterpret_cast<unsigned char *const *>(nav + kNavTrail);
    if (!nav[kNavHasTrail] || !readable(trail, 0x10 + kTrailSize * sizeof(Cookie))) return;
    const int count = *reinterpret_cast<const int *>(trail), last = *reinterpret_cast<const int *>(trail + 4);
    if (count < 2 || count > kTrailSize) return;
    const auto cookie = [&](int n) {  // the n-th oldest
        const int index = count < kTrailSize ? n % count : (last + 1 + n) % kTrailSize;
        return reinterpret_cast<const Cookie *>(trail + 0x10) + index;
    };
    const auto left = [](const Cookie &c) { return Vec3{c.left[0], c.centre[1] + 0.4f, c.left[1]}; };
    const auto right = [](const Cookie &c) { return Vec3{c.right[0], c.centre[1] + 0.4f, c.right[1]}; };
    int nearest = 0;
    float nearest_distance = 1e30f;
    for (int n = 0; n < count; ++n) {
        const Cookie &c = *cookie(n);
        if (car) {
            const float d = std::hypot(c.centre[0] - car[0], c.centre[2] - car[2]);
            if (d < nearest_distance) {
                nearest_distance = d;
                nearest = n;
            }
        }
        if (n + 1 < count) {
            const Cookie &next = *cookie(n + 1);
            lines.add(left(c), left(next), kCookieEdge);
            lines.add(right(c), right(next), kCookieEdge);
        }
        if (n % 4 == 0) lines.add(left(c), right(c), kCookieBar);
    }
    if (!car) return;
    const Cookie &c = *cookie(nearest);
    const float to_car_x = car[0] - c.centre[0], to_car_z = car[2] - c.centre[2];
    const float offset = to_car_x * c.forward[1] - to_car_z * c.forward[0];  // bCross(cookie_to_car, forward): across the road
    const float out_of_bounds = 1.0f + std::max(offset - c.right_offset, c.left_offset - offset);
    const DWORD color = out_of_bounds > 2.0f ? kCookieOut : kCookieEdge;
    lines.add(left(c), right(c), color);
    lines.add({car[0], car[1] + 0.6f, car[2]}, {c.centre[0], c.centre[1] + 0.6f, c.centre[2]}, color);
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
    if (nfsmw::key_pressed(nfsmw::game_has_focus() && nfsmw::key_down(VK_F8), f8_was_down)) {  // Off, AI Logic, Roads, Cost, for both players
        const int next = (settings::player(0).overlay + 1) % 4;
        settings::player(0).overlay = settings::player(1).overlay = next;
        g_log.line("overlay: %s", settings::overlay_name(next));
    }
    const int mode = settings::player(player).overlay;
    road_inspector::set_mode(player, mode);
    if (!view_plat_info || mode == settings::kOverlayOff) return;
    const float *world_clip = reinterpret_cast<const float *>(static_cast<const unsigned char *>(view_plat_info) + 0x80);
    if (!readable(world_clip, 64)) return;

    Lines lines(world_clip, width, height);
    road_inspector::begin_picture();
    if (mode == settings::kOverlayRoads || mode == settings::kOverlayCost) road_inspector::add(lines, player, mode);
    void *ai = control::driving_ai(player);  // (null unless the mode is AI Logic)
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

    if (have_car && settings::player(player).control == settings::kImproved && settings::player(player).avoid_props) obstacles::add_boxes(lines, car);

    if (n) {
        add_route_segments(lines, n, have_car ? car : nullptr);
        add_cookie_trail(lines, n, have_car ? car : nullptr);

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

// What the racing AI of a player's car (ai_control.cpp) is doing, drawn as lines over that player's picture (each player that is AI driven). Every
// element that another part of the AI has changed has a colour (and shape) of its own:
//   cyan     the route the A* search found to the next checkpoint (the road segments of the car's drive-to nav, from the one it is on to
//            the goal, as straight lines between each segment's two nodes: the roads' curves are not followed). Orange where the road's
//            drivable width is under [overlay] reference_width (10 m); a red X and pole where the route turns more than
//            [overlay] sharp_turn_degrees (20 degrees) into the next segment. With [debug] log, every new route is also written to the log, segment by segment.
//   lines from a small white diamond: the roads NOT taken at a junction of the route, coloured by width against the widest choice there (light green
//            widest, yellow >= 75 %, orange >= 50 %, red narrower), as the improved AI's route search compares them (path_cost.cpp)
//   orange   the other roads near the car as corridors (left and right edge over the drivable width, a bar at each end of every segment, curved
//            where the road is): what the AI could steer in on each road it did not plan
//   green    the corridor the car may drive in (the "cookie trail": road cross-sections ahead, each with the road's left and right edge):
//            the edges joined up, and a bar across the road at every cookie
//   red      the part of the corridor that a CAR has cut into (HolePunchAvoidables moved one edge of those cookies inwards so that the car
//            passes on one side): the new edge, an X on every cut cookie, and a pale line from the road's own edge to the new one.
//            Orange instead of red: cut with flag 2 (the car is already beside or behind the AI's nose)
//   yellow   the steering target (IVehicleAI::GetDriveTarget: the point the car steers at) with a pole and a cross, and a line from the car
//   white    the head of the nav (`fPosition`, the look-ahead point the corridor is built up to), a small square with a pole
//   diamond  (the apex) the corner the view along the corridor is blocked by: orange-red when a car's cut is the cause, magenta when it is
//            the road's own edge (a bend); a line from the car to it. The steering target sits beside the apex when there is one.
// Lines are projected with the view's WorldClipMatrix (eViewPlatInfo +0x80, what eViewPlatInterface::GetScreenPosition 0x6BE940 uses; the
// world's axes are swizzled first, x -> -y, z -> x, y -> z) and drawn pre-transformed, with a depth test against the game's depth buffer ([overlay] depth_test), so walls hide them.
// Memory is read with checks: a pointer that is not readable ends the drawing, it never crashes the game.
// What the corridor is and where each value is: agent_docs/ai-driving.md.
#include "overlay.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "ai_control.h"
#include "hooks.h"
#include "road_network.h"
#include "improved_ai_opponents.h"

namespace improved_ai_opponents::overlay {

namespace {

constexpr uintptr_t kSegments = 0x9B38C0, kNodes = 0x9B38BC;
constexpr unsigned kSegmentSize = 0x16, kNodeSize = 0x20;
constexpr int kMaxPathSegments = 80;
constexpr uintptr_t kRoads = 0x9B38C8, kProfiles = 0x9B38B8;
constexpr unsigned kRoadSize = 8, kProfileSize = 0x40;
constexpr unsigned kRacingDrivableLanes = 0xFFFFDF7F;  // drivable_lanes[kLaneRacing]: bit n = lane type n is drivable
constexpr DWORD kRoute = 0xFF00E5FF, kTarget = 0xFFFFE000, kRoadEdge = 0xFF30D030, kRoadBar = 0xFF1C7A1C, kCut = 0xFFFF2020, kCutFlag2 = 0xFFFF8000,
                kPush = 0xFFFFB0B0, kApexCar = 0xFFFF6000, kApexRoad = 0xFFFF00FF, kNavHead = 0xFFFFFFFF, kNarrowRoad = 0xFFFF9A00, kSharpTurn = 0xFFFF3030;

struct Vec3 {
    float x, y, z;
};
struct Vertex {
    float x, y, z, rhw;
    DWORD color;
};
struct Clip {
    float x, y, z, w;
};

// A NavCookie (0x40 bytes, the decomp's layout is the PC's): positions of the edges are (x, z).
struct Cookie {
    float left[2], right[2], forward[2];
    float length, curvature, left_offset, right_offset;
    unsigned flags;
    float pad;
    float centre[3];
    short segment_parameter;
    unsigned short segment;  // bit 15: node
};
static_assert(sizeof(Cookie) == 0x40, "NavCookie");

// WRoadNav, PC offsets.
constexpr unsigned kNavTrail = 0x08, kNavHasTrail = 0x55, kNavRoadOcclusion = 0x58, kNavAvoidableOcclusion = 0x5C, kNavPosition = 0x98, kNavApex = 0x158;
constexpr int kTrailSize = 32;

// The corridor as HolePunchAvoidables left it (a copy of what the AI steers by), for the nav being drawn.
constexpr int kMaxCut = 33;
Cookie g_cut[2][kMaxCut];  // per player
int g_cut_count[2] = {0, 0};
DWORD g_cut_time[2] = {0, 0};
const void *g_watched_nav[2] = {nullptr, nullptr};

using HolePunchFn = void(FASTCALL *)(void *, void *, Cookie *, int, float, float);  // WRoadNav::HolePunchAvoidables 0x775BE0 (this, cookies, count, float, float), ret 0x10
HolePunchFn g_orig_hole_punch = nullptr;

void FASTCALL hook_hole_punch(void *self, void *edx, Cookie *cookies, int count, float a, float b) {
    g_orig_hole_punch(self, edx, cookies, count, a, b);
    for (int player = 0; player < 2; ++player) {
        if (self != g_watched_nav[player] || !cookies || count <= 0 || count > kMaxCut || IsBadReadPtr(cookies, count * sizeof(Cookie))) continue;
        memcpy(g_cut[player], cookies, count * sizeof(Cookie));
        g_cut_count[player] = count;
        g_cut_time[player] = GetTickCount();
    }
}

bool readable(const void *p, size_t n) { return p && !IsBadReadPtr(p, n); }

template <typename T> T *vfn(void *object, unsigned offset) { return reinterpret_cast<T *>((*static_cast<void ***>(object))[offset / 4]); }

const float *call_get_vec(void *object, unsigned offset) {
    using Fn = const float *(__thiscall *)(void *);
    return reinterpret_cast<Fn>(vfn<void>(object, offset))(object);
}

class Lines {
  public:
    Lines(const float *world_clip, float width, float height) : m_(world_clip), width_(width), height_(height) {}

    void add(const Vec3 &a, const Vec3 &b, DWORD color) {
        Clip p = project(a), q = project(b);
        constexpr float kNear = 0.1f;
        if (p.w < kNear && q.w < kNear) return;
        if (p.w < kNear) p = lerp(p, q, (kNear - p.w) / (q.w - p.w));
        else if (q.w < kNear) q = lerp(q, p, (kNear - q.w) / (p.w - q.w));
        const Vertex v0 = screen(p, color), v1 = screen(q, color);
        // A few pixels thick: the same line a pixel over.
        for (int i = 0; i < 4; ++i) {
            const float dx = static_cast<float>(i & 1), dy = static_cast<float>(i >> 1);
            Vertex a2 = v0, b2 = v1;
            a2.x += dx;
            b2.x += dx;
            a2.y += dy;
            b2.y += dy;
            vertices_.push_back(a2);
            vertices_.push_back(b2);
        }
    }
    const std::vector<Vertex> &vertices() const { return vertices_; }

  private:
    Clip project(const Vec3 &sim) const {  // [x y z 1] * M, as D3DXVec4Transform does
        const Vec3 p = {sim.z, -sim.x, sim.y};  // the simulation's axes (y up) to the renderer's (z up): bConvertFromBond
        return {p.x * m_[0] + p.y * m_[4] + p.z * m_[8] + m_[12], p.x * m_[1] + p.y * m_[5] + p.z * m_[9] + m_[13],
                p.x * m_[2] + p.y * m_[6] + p.z * m_[10] + m_[14], p.x * m_[3] + p.y * m_[7] + p.z * m_[11] + m_[15]};
    }
    static Clip lerp(const Clip &from, const Clip &to, float t) {
        return {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t, from.w + (to.w - from.w) * t};
    }
    Vertex screen(const Clip &c, DWORD color) const {
        const float inv = 1.0f / c.w;
        // z for the depth test: the clip depth, a little nearer so that a line on the road is not hidden by the road itself.
        const float z = std::fmin(1.0f, std::fmax(0.0f, c.z * inv - 0.0004f));
        return {(c.x * inv + 1.0f) * width_ * 0.5f, (1.0f - c.y * inv) * height_ * 0.5f, z, 1.0f, color};
    }
    const float *m_;
    float width_, height_;
    std::vector<Vertex> vertices_;
};

void add_pole(Lines &lines, const Vec3 &p, float height, DWORD color);
void add_x(Lines &lines, const Vec3 &p, float r, DWORD color);
void add_diamond(Lines &lines, const Vec3 &p, float r, DWORD color);

bool node_position(unsigned index, Vec3 *out) {
    const unsigned char *nodes = *reinterpret_cast<unsigned char *const *>(kNodes);
    if (!nodes || !readable(nodes + index * kNodeSize, 12)) return false;
    *out = *reinterpret_cast<const Vec3 *>(nodes + index * kNodeSize);
    return true;
}

// What the road is like along a route segment (the quality of the road, as the AI's path search could weigh it).
struct RouteSegment {
    unsigned index = 0, road = 0, flags = 0, start_node = 0, end_node = 0;
    Vec3 from{}, to{};
    float length = 0, width = 0;  // width: the drivable width of the road (racing lanes, metres), the narrower end
    unsigned min_width = 0;       // the road's own minimum width byte
    float turn_deg = 0;           // how far the route turns at `to` into the next segment (0 for the last)
};

float drivable_width(unsigned node_index, char *lanes_text, size_t lanes_size) { return road::drivable_width(node_index, lanes_text, lanes_size); }

// The route of the nav from the segment the car is on to the goal, in travel order, with what the road is like.
std::vector<RouteSegment> route_of(const unsigned char *nav) {
    std::vector<RouteSegment> route;
    if (*reinterpret_cast<const int *>(nav + 0x78) != 3) return route;  // not on a path (no race route yet)
    const int count = *reinterpret_cast<const int *>(nav + 0x2E0);
    const unsigned short *path = *reinterpret_cast<unsigned short *const *>(nav + 0x2E4);
    const unsigned short current = *reinterpret_cast<const unsigned short *>(nav + 0x8E);
    const unsigned short goal = *reinterpret_cast<const unsigned short *>(nav + 0x2DA);
    const unsigned char *segments = *reinterpret_cast<unsigned char *const *>(kSegments);
    const unsigned char *roads = *reinterpret_cast<unsigned char *const *>(kRoads);
    if (count <= 0 || count > 2000 || !readable(path, count * 2) || !segments || !roads) return route;
    std::vector<unsigned> ids;
    bool started = false;
    for (int i = 0; i < count && static_cast<int>(ids.size()) < kMaxPathSegments; ++i) {
        if (path[i] == current) started = true;
        if (!started) continue;
        ids.push_back(path[i]);
        if (path[i] == goal) break;
    }
    unsigned previous_end = 0xFFFFFFFF;
    for (size_t i = 0; i < ids.size(); ++i) {
        const unsigned char *s = segments + ids[i] * kSegmentSize;
        if (!readable(s, kSegmentSize)) break;
        const unsigned a = *reinterpret_cast<const unsigned short *>(s), b = *reinterpret_cast<const unsigned short *>(s + 2);
        unsigned start = a, end = b;
        if (previous_end != 0xFFFFFFFF) {
            if (previous_end == b) std::swap(start, end);
        } else if (i + 1 < ids.size()) {  // the first one ends at the node it shares with the next
            const unsigned char *n = segments + ids[i + 1] * kSegmentSize;
            if (readable(n, kSegmentSize)) {
                const unsigned na = *reinterpret_cast<const unsigned short *>(n), nb = *reinterpret_cast<const unsigned short *>(n + 2);
                if (a == na || a == nb) std::swap(start, end);
            }
        }
        previous_end = end;
        RouteSegment r;
        r.index = ids[i];
        r.start_node = start;
        r.end_node = end;
        r.road = *reinterpret_cast<const short *>(s + 6);
        r.flags = *reinterpret_cast<const unsigned short *>(s + 0xA);
        if (!node_position(start, &r.from) || !node_position(end, &r.to)) continue;
        r.length = std::hypot(r.to.x - r.from.x, r.to.z - r.from.z);
        r.width = std::fmin(drivable_width(start, nullptr, 0), drivable_width(end, nullptr, 0));
        const unsigned char *road = roads + r.road * kRoadSize;
        r.min_width = readable(road, kRoadSize) ? road[5] : 0;
        route.push_back(r);
    }
    for (size_t i = 0; i + 1 < route.size(); ++i) {
        const float ax = route[i].to.x - route[i].from.x, az = route[i].to.z - route[i].from.z;
        const float bx = route[i + 1].to.x - route[i + 1].from.x, bz = route[i + 1].to.z - route[i + 1].from.z;
        const float la = std::hypot(ax, az), lb = std::hypot(bx, bz);
        if (la < 0.01f || lb < 0.01f) continue;
        const float c = std::fmax(-1.0f, std::fmin(1.0f, (ax * bx + az * bz) / (la * lb)));
        route[i].turn_deg = std::acos(c) * 57.29578f;
    }
    return route;
}

bool g_enabled = true;      // [overlay] enabled
bool g_alternative_corridors = true;  // [overlay] alternative_corridors
bool g_depth_test = true;  // [overlay] depth_test
float g_reference_width = 10.0f;  // [overlay] reference_width: roads narrower than this count as narrow (orange)
float g_sharp_turn_deg = 20.0f;   // [overlay] sharp_turn_degrees: a turn of the route sharper than this is marked

// A line of the log for every segment the first time any route has it ([debug] log; to find what makes a road bad; a lap's worth of segments is a few hundred).
void log_route(const std::vector<RouteSegment> &route) {
    static std::vector<unsigned> seen;
    const unsigned char *segments = *reinterpret_cast<unsigned char *const *>(kSegments);
    for (const RouteSegment &r : route) {
        if (std::find(seen.begin(), seen.end(), r.index) != seen.end() || seen.size() > 1500) continue;
        seen.push_back(r.index);
        char lanes[160];
        const unsigned start_node = *reinterpret_cast<const unsigned short *>(segments + r.index * kSegmentSize);
        drivable_width(start_node, lanes, sizeof(lanes));
        g_log.line("ai route seg %u road %d at %.0f %.0f: length %.0f, drivable width %.1f, road min width %u, flags %#x, turn %.0f deg, lanes(type:width) %s", r.index,
                   static_cast<short>(r.road), r.from.x, r.from.z, r.length, r.width, r.min_width, r.flags, r.turn_deg, lanes);
    }
}

// The roads not taken: at each junction of the route where there is a choice, the other roads that lead on from it, coloured by their drivable
// width against the widest of the choices there (the chosen road included): light green the widest (or nearly), yellow at least 75 % of it, orange
// at least 50 %, red narrower. A small white diamond marks the junction. This is the comparison the improved AI's route search makes.
void add_alternatives(Lines &lines, const std::vector<RouteSegment> &route) {
    constexpr DWORD kWidest = 0xFF80FF80, kNearly = 0xFFE0E040, kHalf = 0xFFFF8C00, kNarrow = 0xFFFF2060, kJunction = 0xFFFFFFFF;
    const size_t forks = std::min<size_t>(route.size(), 30);
    for (size_t i = 0; i + 1 < forks; ++i) {
        const unsigned char *junction = road::node(route[i].end_node);
        const float *at = road::node_position(route[i].end_node);
        if (!junction || !at) continue;
        float widest = route[i + 1].width;
        struct Sibling {
            unsigned node;
            float width;
        } siblings[7];
        int count = 0;
        for (int k = 0; k < road::node_segment_count(junction) && k < 7; ++k) {
            const unsigned index = road::node_segment(junction, k);
            if (index == route[i].index || index == route[i + 1].index) continue;
            const unsigned char *sibling = road::segment(index);
            if (!sibling) continue;
            const unsigned a = road::segment_node(sibling, 0), b = road::segment_node(sibling, 1);
            siblings[count++] = {a == route[i].end_node ? b : a, road::segment_width(sibling)};
            widest = std::fmax(widest, siblings[count - 1].width);
        }
        if (count == 0 || widest < 1.0f) continue;
        add_diamond(lines, {at[0], at[1] + 1.0f, at[2]}, 1.0f, kJunction);
        for (int k = 0; k < count; ++k) {
            const float *to = road::node_position(siblings[k].node);
            if (!to) continue;
            const float ratio = siblings[k].width / widest;
            const DWORD color = ratio >= 0.95f ? kWidest : ratio >= 0.75f ? kNearly : ratio >= 0.5f ? kHalf : kNarrow;
            lines.add({at[0], at[1] + 1.0f, at[2]}, {to[0], to[1] + 1.0f, to[2]}, color);
        }
    }
}

// The route: each segment a line between its two nodes. Cyan, orange where the road is narrower than the reference; a red X where the
// route turns sharply into the next segment.
void add_route(Lines &lines, const unsigned char *nav) {
    const std::vector<RouteSegment> route = route_of(nav);
    if (g_debug_log) log_route(route);
    add_alternatives(lines, route);
    for (const RouteSegment &r : route) {
        Vec3 a = r.from, b = r.to;
        a.y += 1.0f;
        b.y += 1.0f;
        lines.add(a, b, r.width < g_reference_width ? kNarrowRoad : kRoute);
        if (r.turn_deg > g_sharp_turn_deg) {
            add_x(lines, {b.x, b.y, b.z}, 1.5f + r.turn_deg / 30.0f, kSharpTurn);
            add_pole(lines, {b.x, b.y, b.z}, 2.0f, kSharpTurn);
        }
    }
}

void add_pole(Lines &lines, const Vec3 &p, float height, DWORD color) { lines.add(p, {p.x, p.y + height, p.z}, color); }

void add_cross(Lines &lines, const Vec3 &p, float r, DWORD color) {
    lines.add({p.x - r, p.y, p.z}, {p.x + r, p.y, p.z}, color);
    lines.add({p.x, p.y, p.z - r}, {p.x, p.y, p.z + r}, color);
}

// Shapes in the ground plane: a diamond (the apex), a square (the nav head), an X (a cut cookie).
void add_diamond(Lines &lines, const Vec3 &p, float r, DWORD color) {
    const Vec3 a = {p.x + r, p.y, p.z}, b = {p.x, p.y, p.z + r}, c = {p.x - r, p.y, p.z}, d = {p.x, p.y, p.z - r};
    lines.add(a, b, color);
    lines.add(b, c, color);
    lines.add(c, d, color);
    lines.add(d, a, color);
}
void add_square(Lines &lines, const Vec3 &p, float r, DWORD color) {
    const Vec3 a = {p.x + r, p.y, p.z + r}, b = {p.x - r, p.y, p.z + r}, c = {p.x - r, p.y, p.z - r}, d = {p.x + r, p.y, p.z - r};
    lines.add(a, b, color);
    lines.add(b, c, color);
    lines.add(c, d, color);
    lines.add(d, a, color);
}
void add_x(Lines &lines, const Vec3 &p, float r, DWORD color) {
    lines.add({p.x - r, p.y, p.z - r}, {p.x + r, p.y, p.z + r}, color);
    lines.add({p.x - r, p.y, p.z + r}, {p.x + r, p.y, p.z - r}, color);
}

Vec3 left_point(const Cookie &c) { return {c.left[0], c.centre[1] + 0.4f, c.left[1]}; }
Vec3 right_point(const Cookie &c) { return {c.right[0], c.centre[1] + 0.4f, c.right[1]}; }

// The stored trail (the road's own corridor, never cut): the n-th oldest cookie.
const Cookie *trail_cookie(const unsigned char *trail, int n) {
    const int count = *reinterpret_cast<const int *>(trail), last = *reinterpret_cast<const int *>(trail + 4);
    const int index = count < kTrailSize ? n % count : (last + 1 + n) % kTrailSize;
    return reinterpret_cast<const Cookie *>(trail + 0x10) + index;
}

// The road's own cookie that a cut one came from (the segment and the place on it are not changed by a cut), or null.
const Cookie *original_of(const unsigned char *trail, int count, const Cookie &cut) {
    for (int n = 0; n < count; ++n) {
        const Cookie *c = trail_cookie(trail, n);
        if (c->segment == cut.segment && c->segment_parameter == cut.segment_parameter) return c;
    }
    return nullptr;
}

float distance2(const float *a, const float *b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }

// What a cookie of the corridor says about which of its edges a car has pulled in.
struct CutInfo {
    bool cut = false, left_moved = false, right_moved = false;
    DWORD color = kCut;
    const Cookie *road = nullptr;
};
CutInfo cut_info(const unsigned char *trail, int stored_count, const Cookie &c) {
    CutInfo info;
    info.cut = (c.flags & 1) != 0;
    info.color = (c.flags & 2) ? kCutFlag2 : kCut;
    if (info.cut) info.road = original_of(trail, stored_count, c);
    if (info.road) {
        info.left_moved = distance2(c.left, info.road->left) > 0.05f;
        info.right_moved = distance2(c.right, info.road->right) > 0.05f;
    }
    return info;
}

void add_corridor(Lines &lines, const unsigned char *nav, int player) {
    const unsigned char *trail = *reinterpret_cast<unsigned char *const *>(nav + kNavTrail);
    if (!nav[kNavHasTrail] || !readable(trail, 0x10 + kTrailSize * sizeof(Cookie))) return;
    const int stored_count = *reinterpret_cast<const int *>(trail);
    if (stored_count < 2 || stored_count > kTrailSize) return;

    // The corridor the AI steered by last, when there is one that is fresh (from the first cookie ahead of the car on); else the road's own.
    const bool have_cut = g_cut_count[player] >= 2 && GetTickCount() - g_cut_time[player] < 500;
    std::vector<Cookie> cookies;
    if (have_cut) cookies.assign(g_cut[player], g_cut[player] + g_cut_count[player]);
    else
        for (int n = 0; n < stored_count; ++n) cookies.push_back(*trail_cookie(trail, n));

    std::vector<CutInfo> info;
    for (const Cookie &c : cookies) info.push_back(cut_info(trail, stored_count, c));

    for (size_t i = 0; i < cookies.size(); ++i) {
        const Cookie &c = cookies[i];
        const CutInfo &ci = info[i];
        lines.add(left_point(c), right_point(c), ci.cut ? ci.color : kRoadBar);  // the bar across the road
        if (ci.cut) {
            Vec3 where = ci.left_moved ? left_point(c) : right_point(c);
            if (!ci.left_moved && !ci.right_moved) where = {c.centre[0], c.centre[1] + 0.4f, c.centre[2]};
            add_x(lines, where, 0.6f, ci.color);
            if (ci.road) {
                if (ci.left_moved) lines.add(left_point(*ci.road), left_point(c), kPush);
                if (ci.right_moved) lines.add(right_point(*ci.road), right_point(c), kPush);
            }
        }
        if (i + 1 < cookies.size()) {
            const Cookie &n = cookies[i + 1];
            const CutInfo &ni = info[i + 1];
            lines.add(left_point(c), left_point(n), (ci.left_moved || ni.left_moved) ? (ni.left_moved ? ni.color : ci.color) : kRoadEdge);
            lines.add(right_point(c), right_point(n), (ci.right_moved || ni.right_moved) ? (ni.right_moved ? ni.color : ci.color) : kRoadEdge);
        }
    }
}

}  // namespace

// The road network around a player's car, all of it (F8 turns it on and off): what the AI's route search has to choose from. A line for every
// segment within 250 m of the car, between its two nodes: light grey the roads the route search can reach from the car, dark grey the ones it never can (the in-race flag is not shown: the search does not use it),
// purple the marked shortcuts, orange the ones that
// cross a player-only barrier (bit 13; the search skips these too); gold chevrons on one-way segments point the way they can be driven. A small white
// diamond at each road node that has a drawn connection (all nodes, not only where roads branch). Segments that cross a track barrier (bit 12: outside this race) are not drawn.
bool g_roads_view = false;
// The segments and junction nodes near a centre are found once (a scan of the whole network) and kept until the car is 50 m from that centre.
struct Near {  // what is near a car (a slot per player: two cars far apart would otherwise rebuild it every picture)
    std::vector<unsigned> segments, junctions;
    std::vector<char> reachable;  // per segment: the route search could use it from the car's road (see mark_reachable)
    float centre[2] = {1e9f, 1e9f};
};
Near g_near[2];

// Which segments the route search can reach from the car at all, by the game's rules for a racing car (AStarSearch::Admissible): not a segment that
// crosses a barrier (bits 12, 13), a one-way segment only from its first node to its second (the shortcut rule is lifted for the improved AI).
// Costs are not looked at: a segment that is not marked here can never be on the AI's route, however good it looks.
void mark_reachable(const float *car, std::vector<char> &reachable) {
    const unsigned segments = road::segment_count(), nodes = road::node_count();
    reachable.assign(segments, 0);
    unsigned nearest = segments;
    float best = 1e30f;
    for (unsigned i = 0; i < segments; ++i) {
        const unsigned char *s = road::segment(i);
        const float *a = road::node_position(road::segment_node(s, 0)), *b = road::node_position(road::segment_node(s, 1));
        if (!a || !b) continue;
        const float dx = b[0] - a[0], dz = b[2] - a[2], l2 = dx * dx + dz * dz;
        const float t = l2 > 1e-3f ? std::fmax(0.0f, std::fmin(1.0f, ((car[0] - a[0]) * dx + (car[2] - a[2]) * dz) / l2)) : 0.0f;
        const float d = std::hypot(car[0] - (a[0] + t * dx), car[2] - (a[2] + t * dz));
        if (d < best) best = d, nearest = i;
    }
    if (nearest >= segments) return;
    std::vector<char> seen(nodes, 0);
    std::vector<unsigned> queue;
    const unsigned char *start = road::segment(nearest);
    for (int k = 0; k < 2; ++k) {
        if (k == 0 && (road::segment_flags(start) & 0x40)) continue;  // one-way: the car goes out of the second node
        const unsigned n = road::segment_node(start, k);
        if (n < nodes && !seen[n]) seen[n] = 1, queue.push_back(n);
    }
    reachable[nearest] = 1;
    for (size_t head = 0; head < queue.size(); ++head) {
        const unsigned char *node = road::node(queue[head]);
        for (int k = 0; k < road::node_segment_count(node) && k < 8; ++k) {
            const unsigned index = road::node_segment(node, k);
            const unsigned char *s = road::segment(index);
            if (!s || (road::segment_flags(s) & 0x3000)) continue;
            const bool forward = road::segment_node(s, 0) == queue[head];
            if ((road::segment_flags(s) & 0x40) && !forward) continue;
            reachable[index] = 1;
            const unsigned next = road::segment_node(s, forward ? 1 : 0);
            if (next < nodes && !seen[next]) seen[next] = 1, queue.push_back(next);
        }
    }
}
constexpr float kNearRadius = 250.0f, kNearRebuild = 50.0f;
Near &ensure_near(int slot, const float *car) {
    Near &nearby = g_near[slot != 0 ? 1 : 0];
    constexpr float kRadius = kNearRadius, kRebuild = kNearRebuild;
    if (std::hypot(car[0] - nearby.centre[0], car[2] - nearby.centre[1]) > kRebuild) {
        nearby.centre[0] = car[0];
        nearby.centre[1] = car[2];
        nearby.segments.clear();
        nearby.junctions.clear();
        mark_reachable(car, nearby.reachable);
        for (unsigned i = 0; i < road::segment_count() && nearby.segments.size() < 3000; ++i) {
            const unsigned char *s = road::segment(i);
            if (road::segment_flags(s) & 0x1000) continue;  // crosses a track barrier: outside this race, not drawn
            const float *pa = road::node_position(road::segment_node(s, 0)), *pb = road::node_position(road::segment_node(s, 1));
            if (!pa || !pb) continue;
            if (std::hypot(pa[0] - car[0], pa[2] - car[2]) <= kRadius + kRebuild || std::hypot(pb[0] - car[0], pb[2] - car[2]) <= kRadius + kRebuild)
                nearby.segments.push_back(i);
        }
        for (unsigned i = 0; i < road::node_count(); ++i) {
            const unsigned char *n = road::node(i);
            const float *p = reinterpret_cast<const float *>(n);
            if (std::hypot(p[0] - car[0], p[2] - car[2]) > kRadius + kRebuild) continue;
            bool drawn = false;  // a node is only marked when one of its connections is drawn
            for (int k = 0; k < road::node_segment_count(n) && k < 7 && !drawn; ++k) {
                const unsigned char *seg = road::segment(road::node_segment(n, k));
                drawn = seg && !(road::segment_flags(seg) & 0x1000);
            }
            if (drawn) nearby.junctions.push_back(i);
        }
    }
    return nearby;
}

// The other roads near the car as corridors (not the ones on the route: the green corridor is those): for each segment its left and right edge over
// the road's drivable width (the width of the node's profile at each end, as the nav's corridor takes it, centred on the road) and a bar across each
// end, orange. A curved segment (flag 8) is a cubic Bezier through its two nodes with the control points the node plus the segment's start / end
// handle (WRoadNetwork::BuildSegmentSpline: start + StartControl, end + EndControl; the handle is a 3 byte direction times a 16 bit length), which is
// followed here; a straight one is a line. These are the corridors the AI could have steered in on each road, without what the game adds while it
// drives (the cuts around cars, the funnel). A road that has no corridor here is a road the AI never has to choose.
Vec3 bezier_point(const Vec3 (&p)[4], float t) {
    const float u = 1.0f - t, a = u * u * u, b = 3.0f * u * u * t, c = 3.0f * u * t * t, d = t * t * t;
    return {a * p[0].x + b * p[1].x + c * p[2].x + d * p[3].x, a * p[0].y + b * p[1].y + c * p[2].y + d * p[3].y, a * p[0].z + b * p[1].z + c * p[2].z + d * p[3].z};
}
Vec3 bezier_tangent(const Vec3 (&p)[4], float t) {
    const float u = 1.0f - t, a = 3.0f * u * u, b = 6.0f * u * t, c = 3.0f * t * t;
    return {a * (p[1].x - p[0].x) + b * (p[2].x - p[1].x) + c * (p[3].x - p[2].x), a * (p[1].y - p[0].y) + b * (p[2].y - p[1].y) + c * (p[3].y - p[2].y),
            a * (p[1].z - p[0].z) + b * (p[2].z - p[1].z) + c * (p[3].z - p[2].z)};
}

void add_alternative_corridors(Lines &lines, const float *car, int slot, const std::vector<unsigned> &skip) {
    constexpr float kRange = 160.0f;
    constexpr DWORD kEdge = 0xFFFFA030;
    constexpr float kHandleScale = 500.0f / (127.0f * 65535.0f);
    const Near &nearby = ensure_near(slot, car);
    for (const unsigned i : nearby.segments) {
        if (std::find(skip.begin(), skip.end(), i) != skip.end()) continue;
        const unsigned char *s = road::segment(i);
        const unsigned a = road::segment_node(s, 0), b = road::segment_node(s, 1);
        const float *pa = road::node_position(a), *pb = road::node_position(b);
        if (!pa || !pb) continue;
        if (std::hypot(pa[0] - car[0], pa[2] - car[2]) > kRange && std::hypot(pb[0] - car[0], pb[2] - car[2]) > kRange) continue;
        const float wa = 0.5f * road::drivable_width(a), wb = 0.5f * road::drivable_width(b);
        if (wa < 0.25f && wb < 0.25f) continue;
        Vec3 p[4] = {{pa[0], pa[1], pa[2]}, {pa[0], pa[1], pa[2]}, {pb[0], pb[1], pb[2]}, {pb[0], pb[1], pb[2]}};
        float length = std::hypot(pb[0] - pa[0], pb[2] - pa[2]);
        if (road::segment_flags(s) & 0x100) {  // curved
            const float end_scale = *reinterpret_cast<const unsigned short *>(s + 0xC) * kHandleScale, start_scale = *reinterpret_cast<const unsigned short *>(s + 0xE) * kHandleScale;
            const signed char *end_handle = reinterpret_cast<const signed char *>(s + 0x10), *start_handle = reinterpret_cast<const signed char *>(s + 0x13);
            p[1] = {pa[0] + start_scale * start_handle[0], pa[1] + start_scale * start_handle[1], pa[2] + start_scale * start_handle[2]};
            p[2] = {pb[0] + end_scale * end_handle[0], pb[1] + end_scale * end_handle[1], pb[2] + end_scale * end_handle[2]};
            length = std::hypot(p[1].x - p[0].x, p[1].z - p[0].z) + std::hypot(p[2].x - p[1].x, p[2].z - p[1].z) + std::hypot(p[3].x - p[2].x, p[3].z - p[2].z);
        }
        if (length < 0.5f) continue;
        const int steps = std::clamp(static_cast<int>(length / 5.0f) + 1, 2, 24);
        Vec3 last_left{}, last_right{};
        for (int k = 0; k <= steps; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(steps);
            const Vec3 c = bezier_point(p, t), tangent = bezier_tangent(p, t);
            const float tl = std::hypot(tangent.x, tangent.z);
            if (tl < 1e-4f) continue;
            const float w = wa + (wb - wa) * t, px = -tangent.z / tl, pz = tangent.x / tl;
            const Vec3 left = {c.x + px * w, c.y + 0.3f, c.z + pz * w}, right = {c.x - px * w, c.y + 0.3f, c.z - pz * w};
            if (k > 0) {
                lines.add(last_left, left, kEdge);
                lines.add(last_right, right, kEdge);
            }
            if (k == 0 || k == steps) lines.add(left, right, kEdge);
            last_left = left;
            last_right = right;
        }
    }
}

void add_roads_around(Lines &lines, const float *car, int slot) {
    constexpr DWORD kRoad = 0xFFE0E0E0, kUnreachable = 0xFF605060, kShortcut = 0xFFC050FF, kJunction = 0xFFFFFFFF;
    constexpr DWORD kPlayerBarrier = 0xFFFF9020, kOneWay = 0xFFFFD040;
    const Near &nearby = ensure_near(slot, car);
    for (const unsigned i : nearby.segments) {
        const unsigned char *s = road::segment(i);
        const float *pa = road::node_position(road::segment_node(s, 0)), *pb = road::node_position(road::segment_node(s, 1));
        if (!pa || !pb) continue;
        const unsigned flags = road::segment_flags(s);
        const DWORD color = (flags & 0x2000) ? kPlayerBarrier : (flags & 0x80) ? kShortcut : (i < nearby.reachable.size() && nearby.reachable[i]) ? kRoad : kUnreachable;
        lines.add({pa[0], pa[1] + 0.5f, pa[2]}, {pb[0], pb[1] + 0.5f, pb[2]}, color);
        if (flags & 0x40) {  // one-way: a chevron mid-segment, pointing the way the segment may be driven (node 0 to node 1)
            const float dx = pb[0] - pa[0], dz = pb[2] - pa[2], l = std::hypot(dx, dz);
            if (l > 6.0f) {
                const float ux = dx / l, uz = dz / l, mx = 0.5f * (pa[0] + pb[0]), my = 0.5f * (pa[1] + pb[1]) + 0.5f, mz = 0.5f * (pa[2] + pb[2]);
                lines.add({mx, my, mz}, {mx - 2.0f * ux - 1.5f * uz, my, mz - 2.0f * uz + 1.5f * ux}, kOneWay);
                lines.add({mx, my, mz}, {mx - 2.0f * ux + 1.5f * uz, my, mz - 2.0f * uz - 1.5f * ux}, kOneWay);
            }
        }
    }
    for (const unsigned i : nearby.junctions) {
        const float *p = road::node_position(i);
        add_diamond(lines, {p[0], p[1] + 1.0f, p[2]}, 1.0f, kJunction);
    }
}

void install() {
    g_enabled = ini_flag("overlay", "enabled");
    if (!g_enabled) return;  // no corridor hook either
    g_depth_test = GetPrivateProfileIntA("overlay", "depth_test", 1, g_ini) != 0;
    g_alternative_corridors = GetPrivateProfileIntA("overlay", "alternative_corridors", 1, g_ini) != 0;
    g_reference_width = static_cast<float>(GetPrivateProfileIntA("overlay", "reference_width", 10, g_ini));
    g_sharp_turn_deg = static_cast<float>(GetPrivateProfileIntA("overlay", "sharp_turn_degrees", 20, g_ini));
    const nfsmw::Hook hooks[] = {
        {"ai_corridor", "WRoadNav::HolePunchAvoidables", 0x775BE0, {0x81, 0xEC, 0xBC, 0x01, 0x00, 0x00, 0x55}, false, nfsmw::detour(&hook_hole_punch),
         nfsmw::orig(&g_orig_hole_punch)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "ai overlay");
}

void draw_lines(IDirect3DDevice9 *device, const Lines &lines) {
    if (lines.vertices().empty()) return;
    IDirect3DStateBlock9 *saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
    saved->Capture();
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetRenderState(D3DRS_ZENABLE, g_depth_test ? D3DZB_TRUE : D3DZB_FALSE);  // behind a wall the lines are hidden (the game's depth buffer)
    device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->DrawPrimitiveUP(D3DPT_LINELIST, static_cast<UINT>(lines.vertices().size() / 2), lines.vertices().data(), sizeof(Vertex));
    saved->Apply();
    saved->Release();
}

void draw(IDirect3DDevice9 *device, const void *view_plat_info, float width, float height, int player) {
    if (!g_enabled) return;
    static bool f8_was_down = false;
    const bool f8_down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (f8_down && !f8_was_down) g_roads_view = !g_roads_view;
    f8_was_down = f8_down;
    void *ai = control::driving_ai(player);
    if (!view_plat_info || (!ai && !g_roads_view)) return;
    const float *world_clip = reinterpret_cast<const float *>(static_cast<const unsigned char *>(view_plat_info) + 0x80);
    if (!readable(world_clip, 64)) return;

    Lines lines(world_clip, width, height);
    using GetFn = void *(__thiscall *)(void *);
    if (g_roads_view) {
        void *any = control::vehicle_ai(player);
        void *simable = any ? reinterpret_cast<GetFn>(vfn<void>(any, 0x04))(any) : nullptr;
        void *body = simable ? reinterpret_cast<GetFn>(vfn<void>(simable, 0x54))(simable) : nullptr;
        const float *at = body ? call_get_vec(body, 0x20) : nullptr;
        if (readable(at, 12)) add_roads_around(lines, at, player);
    }
    if (!ai) {
        if (!lines.vertices().empty()) draw_lines(device, lines);
        return;
    }
    void *nav = reinterpret_cast<GetFn>(vfn<void>(ai, 0x48))(ai);
    const unsigned char *n = readable(nav, 0x300) ? static_cast<const unsigned char *>(nav) : nullptr;
    g_watched_nav[player] = n;

    const float *target = call_get_vec(ai, 0x44);
    void *simable = reinterpret_cast<GetFn>(vfn<void>(ai, 0x04))(ai);
    void *body = simable ? reinterpret_cast<GetFn>(vfn<void>(simable, 0x54))(simable) : nullptr;
    const float *car = body ? call_get_vec(body, 0x20) : nullptr;
    const bool have_car = readable(car, 12);

    // The roads near the car, then the route and the corridor, the rest over them.
    if (n) {
        if (have_car && g_alternative_corridors) {
            std::vector<unsigned> on_route;
            for (const RouteSegment &r : route_of(n)) on_route.push_back(r.index);
            add_alternative_corridors(lines, car, player, on_route);
        }
        add_route(lines, n);
        add_corridor(lines, n, player);
    }

    if (n) {
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

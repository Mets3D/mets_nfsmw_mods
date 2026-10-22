// The game plans a car's route once and follows it: AIActionRace plans a new one only while the drive-to nav is not on a path (nav type 3), so every new route
// begins with the nav dropping its path (see agent_docs/ai-driving.md, "The route search: what it uses and when it runs"). It drops it
//  - when it steps on from the end of a segment and the segment it is on is not in the path (WRoadNav::GetNextOffset), or the path has ended: the goal is reached;
//  - when WRoadNav::CancelPathFinding runs: AIVehicle::ResetDriveToNav (the nav is put on the road nearest to the car again) does, and that is called by
//    AIActionRace::CheckOffPath every 0.25 s when the nav is out of bounds (more than 2 m) and the road the car is on is neither in the nav's cookie trail nor in
//    its path, when a race begins or ends, by the unstick action, and by our F9.
// This watches the nav every frame and, when its path is gone, writes down what it was before, where the nav and the car are now, and which of those it looks like.
#include "route_watch.h"

#include <algorithm>
#include <vector>
#include <windows.h>

#include "improved_ai_opponents.h"
#include "road_geometry.h"

namespace improved_ai_opponents::route_watch {

namespace {

constexpr unsigned kNavType = 0x78, kNavSegment = 0x8E, kNavPosition = 0x98, kNavGoal = 0x2DA, kNavPathCount = 0x2E0, kNavPath = 0x2E4;
constexpr int kPathType = 3;

struct Watch {
    bool on_path = false, asked = false, waiting = false;  // waiting: dropped, the next route is not there yet
    DWORD dropped_at = 0;
    float dropped_nav[3] = {};
    std::vector<unsigned short> path;
    const void *path_pointer = nullptr;
    int path_count = 0;
    unsigned short goal = 0, segment = 0;  // the goal segment of the route, the segment the nav was on
    float nav_at[3] = {}, car_at[3] = {};
};
Watch g_watch[2];

template <typename T> T read(const void *base, unsigned offset) { return *reinterpret_cast<const T *>(static_cast<const unsigned char *>(base) + offset); }

// The segment whose road is nearest to a point (in 3D), and how far.
int nearest_segment(const float point[3], float *distance) {
    int best = -1;
    float best_distance = 1e30f;
    for (unsigned i = 0; i < road::segment_count(); ++i) {
        geometry::SegmentGeometry g;
        if (!geometry::segment_geometry(i, &g)) continue;
        const float mx = 0.5f * (g.node_a.x + g.node_b.x) - point[0], my = 0.5f * (g.node_a.y + g.node_b.y) - point[1], mz = 0.5f * (g.node_a.z + g.node_b.z) - point[2];
        if (std::sqrt(mx * mx + my * my + mz * mz) > 100.0f + 0.5f * g.between) continue;
        const geometry::Nearest n = geometry::nearest_on_curve(g, point);
        if (n.distance < best_distance) {
            best_distance = n.distance;
            best = static_cast<int>(i);
        }
    }
    *distance = best_distance;
    return best;
}

int index_in(const std::vector<unsigned short> &path, unsigned segment) {
    const auto found = std::find(path.begin(), path.end(), segment);
    return found == path.end() ? -1 : static_cast<int>(found - path.begin());
}

}  // namespace

void requested(int player) { g_watch[player != 0 ? 1 : 0].asked = true; }

void frame(int player, const void *nav, const float *car) {
    Watch &w = g_watch[player != 0 ? 1 : 0];
    if (!nav || IsBadReadPtr(nav, 0x300) || !car || !road::loaded()) {
        w.on_path = false;
        return;
    }
    const int count = read<int>(nav, kNavPathCount);
    const unsigned short *path = read<const unsigned short *>(nav, kNavPath);
    const bool on_path = read<int>(nav, kNavType) == kPathType && count > 0 && count <= 2000 && !IsBadReadPtr(path, count * 2);
    if (on_path && w.waiting) {  // the route the game planned after a drop: from where the nav had got to by then
        w.waiting = false;
        const float *now = reinterpret_cast<const float *>(static_cast<const unsigned char *>(nav) + kNavPosition);
        g_log.line("route watch: player %d's new route of %d segments came %d ms after the drop; the nav had moved %.0f m on by then (it was on segment %u, it starts from segment %u)", player + 1, count,
                   static_cast<int>(GetTickCount() - w.dropped_at), std::hypot(now[0] - w.dropped_nav[0], now[2] - w.dropped_nav[2]), w.segment, path[0]);
    }
    if (on_path) {
        if (path != w.path_pointer || count != w.path_count) {  // a route is new (or changed): the copy is what the nav had
            w.path.assign(path, path + count);
            w.path_pointer = path;
            w.path_count = count;
        }
        w.on_path = true;
        w.goal = read<unsigned short>(nav, kNavGoal);
        w.segment = read<unsigned short>(nav, kNavSegment);
        memcpy(w.nav_at, static_cast<const unsigned char *>(nav) + kNavPosition, 12);
        memcpy(w.car_at, car, 12);
        return;
    }
    if (!w.on_path) return;
    w.on_path = false;
    w.path_pointer = nullptr;
    w.path_count = 0;
    w.waiting = true;
    w.dropped_at = GetTickCount();
    memcpy(w.dropped_nav, w.nav_at, 12);

    const int at = index_in(w.path, w.segment);
    const char *why;
    if (w.asked) why = "asked for (F9)";
    else if (at < 0) why = "the nav was on a segment that is not on the route";
    else if (at + 1 >= static_cast<int>(w.path.size()) || w.segment == w.goal) why = "the end of the route: the goal was reached";
    else why = "a reset with the nav still on the route (the nav out of bounds, a race start, unstuck...)";
    w.asked = false;
    float car_distance = 0.0f;
    const int car_segment = nearest_segment(w.car_at, &car_distance);
    g_log.line("route watch: player %d's route of %zu segments dropped: %s. The nav was on segment %u (%d of the route) at %.0f %.0f, the car at %.0f %.0f, on the road of segment %d "
               "(%d of the route, %.1f m away); the nav is now on segment %u",
               player + 1, w.path.size(), why, w.segment, at, w.nav_at[0], w.nav_at[2], w.car_at[0], w.car_at[2], car_segment, car_segment < 0 ? -1 : index_in(w.path, car_segment), car_distance,
               read<unsigned short>(nav, kNavSegment));
}

}  // namespace improved_ai_opponents::route_watch

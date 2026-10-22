// A route is a plan from where the nav is to a goal, and the game plans the next one only when the nav has no path and the AI's target has moved on (AIActionRace::
// Update, when the car passes the checkpoint: the nav is 30 to 100 m ahead of the car, so it reaches the goal first). Until then the nav has no path and takes the
// first branch it is allowed at every node (see route_watch.cpp, agent_docs/ai-driving.md). So the improved AI does not let its route run out: when what is left
// of it is short, a search is started towards a point further along the race route, from the nav's place (WRoadNav::FindPath 0x788F30 only submits a search: the
// route stays what the nav follows while it runs, and is replaced by the new one when it is found).
// Where that point is: the race route is the segments flagged in race, each with the direction the race goes along it (the in-race and race-forward flags, set when
// the race loads: WRoadNetwork::AddRaceSegments). From the end of the route that chain is followed for kAhead metres; where it ends (a sprint's finish) or forks
// nothing is asked for.
#include "route_extend.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "improved_ai_opponents.h"

namespace improved_ai_opponents::route_extend {

namespace {

constexpr uintptr_t kFindPath = 0x788F30, kFindingPath = 0x770090;
constexpr unsigned kNavType = 0x78, kNavSegment = 0x8E, kNavPathCount = 0x2E0, kNavPath = 0x2E4;
constexpr int kPathType = 3, kMaxPath = 500;  // (the path buffer the game makes holds 510 segments)
constexpr float kMinRemaining = 1800.0f;      // metres of route left when the next search is started
constexpr float kAhead = 3000.0f;             // how far along the race route the search is asked to go (from the end of the route)
constexpr DWORD kBackoff = 2500;              // ms between two searches for a player

DWORD g_last[2] = {0, 0}, g_submitted[2] = {0, 0};  // when the last search was started; when the one that is running was (0: none)

template <typename T> T read(const void *base, unsigned offset) { return *reinterpret_cast<const T *>(static_cast<const unsigned char *>(base) + offset); }

float length_of(unsigned segment) {
    const unsigned char *s = road::segment(segment);
    return s ? *reinterpret_cast<const unsigned short *>(s + 4) * (1000.0f / 65535.0f) : 0.0f;
}

// The node at the end of the route (the node of the last segment that the one before does not share; for one segment, the one farther from the car), or -1.
int end_node_of(const unsigned short *path, int count, const float *car) {
    const unsigned char *last = road::segment(path[count - 1]);
    if (!last) return -1;
    const unsigned a = road::segment_node(last, 0), b = road::segment_node(last, 1);
    if (count >= 2) {
        const unsigned char *before = road::segment(path[count - 2]);
        if (!before) return -1;
        const bool a_shared = a == road::segment_node(before, 0) || a == road::segment_node(before, 1);
        const bool b_shared = b == road::segment_node(before, 0) || b == road::segment_node(before, 1);
        if (a_shared == b_shared) return -1;
        return a_shared ? static_cast<int>(b) : static_cast<int>(a);
    }
    const float *pa = road::node_position(a), *pb = road::node_position(b);
    if (!pa || !pb) return -1;
    const float da = std::hypot(pa[0] - car[0], pa[2] - car[2]), db = std::hypot(pb[0] - car[0], pb[2] - car[2]);
    return static_cast<int>(da > db ? a : b);
}

// Follows the race route from `node` (not back along the segment `came_by`) for `ahead` metres: the node it gets to, or -1 when the race route ends or forks within
// 150 m of it.
int follow_race_route(int node, unsigned came_by, float ahead, float *distance) {
    float total = 0.0f;
    unsigned previous = came_by;
    for (int step = 0; step < 200 && total < ahead; ++step) {
        const unsigned char *n = road::node(static_cast<unsigned>(node));
        if (!n) break;
        unsigned next = 0xFFFFFFFF;
        int options = 0;
        for (int i = 0; i < std::min(road::node_segment_count(n), 7); ++i) {
            const unsigned index = road::node_segment(n, i);
            const unsigned char *s = road::segment(index);
            if (!s || index == previous) continue;
            const unsigned flags = road::segment_flags(s);
            if (!(flags & road::kFlagInRace) || (flags & road::kFlagCrossesBarrier)) continue;
            const bool leaving_from_first = road::segment_node(s, 0) == static_cast<unsigned>(node);
            if (((flags & road::kFlagRaceForward) != 0) != leaving_from_first) continue;
            next = index;
            ++options;
        }
        if (options != 1) break;  // the race route ends, or forks
        total += length_of(next);
        node = static_cast<int>(road::other_node(road::segment(next), static_cast<unsigned>(node)));
        previous = next;
    }
    *distance = total;
    return total >= 150.0f ? node : -1;
}

}  // namespace

void frame(int player, void *nav, const float *car) {
    DWORD &last = g_last[player != 0 ? 1 : 0], &submitted = g_submitted[player != 0 ? 1 : 0];
    if (!nav || IsBadReadPtr(nav, 0x300) || !car || !road::loaded() || read<int>(nav, kNavType) != kPathType) return;
    const int count = read<int>(nav, kNavPathCount);
    const unsigned short *path = read<const unsigned short *>(nav, kNavPath);
    if (count <= 0 || count > kMaxPath || IsBadReadPtr(path, count * 2)) return;
    const DWORD now = GetTickCount();
    if (now - last < kBackoff) return;
    if (reinterpret_cast<bool(__thiscall *)(void *)>(kFindingPath)(nav)) return;  // a search is running
    if (submitted) {
        g_log.line("route extend: player %d's search took %d ms; the route is now %d segments", player + 1, static_cast<int>(now - submitted), count);
        submitted = 0;
    }

    const unsigned current = read<unsigned short>(nav, kNavSegment);
    float remaining = 0.0f;
    bool found = false;
    for (int i = 0; i < count; ++i) {
        found = found || path[i] == current;
        if (found) remaining += length_of(path[i]);
    }
    if (!found || remaining > kMinRemaining) return;

    const int end = end_node_of(path, count, car);
    float ahead = 0.0f;
    const int goal = end < 0 ? -1 : follow_race_route(end, path[count - 1], kAhead, &ahead);
    last = now;
    if (goal < 0) return;
    const float *at = road::node_position(static_cast<unsigned>(goal));
    if (!at) return;
    g_log.line("route extend: player %d's route of %d segments has %.0f m left: planning on %.0f m further, to node %d at %.0f %.0f", player + 1, count, remaining, ahead, goal, at[0], at[2]);
    if (reinterpret_cast<bool(__thiscall *)(void *, const float *, const float *, char *)>(kFindPath)(nav, at, nullptr, nullptr)) submitted = now;
}

}  // namespace improved_ai_opponents::route_extend

// The game's road network as the PC build lays it out (agent_docs/ai-driving.md): nodes (0x20 bytes, [0x9B38BC]), segments (0x16 bytes,
// [0x9B38C0]) and road profiles (0x40 bytes, [0x9B38B8]). Read-only helpers that check what they read.
#pragma once

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace nfsmw::road {

constexpr uintptr_t kNodes = 0x9B38BC, kSegments = 0x9B38C0, kProfiles = 0x9B38B8, kRoads = 0x9B38C8;
constexpr uintptr_t kRoadNetwork = 0x9B38B4, kValid = 0x9B3828;  // WRoadNetwork::fgRoadNetwork (null between races) / fValid
constexpr uintptr_t kNumSegments = 0x9B3874, kNumNodes = 0x9B387C, kNumProfiles = 0x9B3884;  // WRoadNetwork::fNumSegments / fNumNodes
constexpr unsigned kNodeSize = 0x20, kSegmentSize = 0x16, kProfileSize = 0x40, kRoadSize = 8;
constexpr unsigned kRacingDrivableLanes = 0xFFFFDF7F;  // drivable_lanes[kLaneRacing]: bit n = lane type n is drivable
constexpr uintptr_t kValidRaceFilter = 0x9B3829;  // WRoadNetwork::fValidRaceFilter: the segments' in-race flags are set
// WRoadSegment flags (+0xA)
constexpr unsigned kFlagRaceForward = 0x4, kFlagOneWay = 0x40, kFlagShortcut = 0x80, kFlagCurved = 0x100, kFlagEndInverted = 0x200, kFlagStartInverted = 0x400,
                   kFlagCrossesBarrier = 0x3000, kFlagInRace = 0x8000;
constexpr unsigned kRoughLanes = 0x4;  // lane type 2: seen as sidewalks, medians and shoulders; drivable for a racing car, but not where it would choose to drive
constexpr unsigned kRoadLanes = kRacingDrivableLanes & ~kRoughLanes;  // the road itself: the other lanes a racing car may drive on (types 1 and 10, the ones it picks
// its own lane from (selectable_lanes[kLaneRacing] 0x402), but also the others seen on roads, such as 3 and 0)

inline bool readable(const void *p, size_t n) { return p && !IsBadReadPtr(p, n); }

// The network is there: the arrays and counts below are left as they were when a race unloads (the memory is freed), so nothing may read them
// while this is false (it crashed the game on the way back to the main menu).
inline bool loaded() { return *reinterpret_cast<void *const *>(kRoadNetwork) != nullptr && *reinterpret_cast<const unsigned char *>(kValid) != 0; }
inline unsigned node_count() { return loaded() ? *reinterpret_cast<const unsigned *>(kNumNodes) : 0; }
inline unsigned segment_count() { return loaded() ? *reinterpret_cast<const unsigned *>(kNumSegments) : 0; }
// The tables are checked against their counts, not with IsBadReadPtr (slow, and it trips guard pages: it crashed the game).
inline const unsigned char *node(unsigned index) {
    const unsigned char *base = *reinterpret_cast<unsigned char *const *>(kNodes);
    return base && index < node_count() ? base + index * kNodeSize : nullptr;
}
inline const unsigned char *segment(unsigned index) {
    const unsigned char *base = *reinterpret_cast<unsigned char *const *>(kSegments);
    return base && index < segment_count() ? base + index * kSegmentSize : nullptr;
}

// A node's position (x, y, z: the simulation's axes).
inline const float *node_position(unsigned index) {
    const unsigned char *n = node(index);
    return n ? reinterpret_cast<const float *>(n) : nullptr;
}
inline unsigned segment_node(const unsigned char *segment, int which) { return *reinterpret_cast<const unsigned short *>(segment + 2 * which); }
inline unsigned segment_flags(const unsigned char *segment) { return *reinterpret_cast<const unsigned short *>(segment + 0xA); }
// The other node of a segment, as seen from `node`.
inline unsigned other_node(const unsigned char *segment, unsigned node) {
    const unsigned a = segment_node(segment, 0), b = segment_node(segment, 1);
    return a == node ? b : a;
}
inline int node_segment_count(const unsigned char *node) { return node[0x10]; }
inline unsigned node_segment(const unsigned char *node, int i) { return *reinterpret_cast<const unsigned short *>(node + 0x12 + 2 * i); }

// The drivable width of a node's road profile for a racing car (metres): the widths of the lanes of the types a racing car may drive on.
// `lanes_text`, if given, gets every lane as "type:width".
inline float drivable_width(unsigned node_index, char *lanes_text = nullptr, size_t lanes_size = 0) {
    if (lanes_text && lanes_size) lanes_text[0] = 0;
    const unsigned char *n = node(node_index);
    const unsigned char *profiles = *reinterpret_cast<unsigned char *const *>(kProfiles);
    if (!n || !profiles) return 0.0f;
    const short profile_index = *reinterpret_cast<const short *>(n + 0xE);
    const unsigned char *profile = profiles + profile_index * kProfileSize;
    if (profile_index < 0 || static_cast<unsigned>(profile_index) >= *reinterpret_cast<const unsigned *>(kNumProfiles)) return 0.0f;
    const int lanes = profile[0];
    float width = 0.0f;
    size_t used = 0;
    for (int i = 0; i < lanes && i < 15; ++i) {
        const unsigned bits = *reinterpret_cast<const unsigned *>(profile + 4 + 4 * i);
        const unsigned type = bits & 15;
        const int raw = static_cast<int>(bits << 14) >> 18;  // 14 bits from bit 4, signed
        const float lane_width = static_cast<float>(raw) * (100.0f / 8191.0f);
        if ((kRacingDrivableLanes >> type) & 1) width += lane_width;
        if (lanes_text && used + 16 < lanes_size) used += snprintf(lanes_text + used, lanes_size - used, "%u:%.1f ", type, lane_width);
    }
    return width;
}

// One lane of a road profile across the road (the game's WRoadLane; it calls the count and the middle fNumZones and fMiddleZone), as the nav reads it
// (WRoadProfile::GetLaneType / GetRelativeLaneOffset / GetLaneWidth): its type and the span `from` .. `to` (metres) along the right vector, measured from the
// node (lanes before the middle lane are on the negative side).
struct Lane {
    int type;
    float from, to;
};
// The lanes of the profile of a node, in the order the game's nav numbers them for a segment end that is `inverted` (the segment's start / end inverted flag).
// Returns how many (at most 15); `middle` gets the middle lane's number.
inline int profile_lanes(unsigned node_index, bool inverted, Lane out[15], int *middle = nullptr) {
    const unsigned char *n = node(node_index);
    const unsigned char *profiles = *reinterpret_cast<unsigned char *const *>(kProfiles);
    if (!n || !profiles) return 0;
    const short profile_index = *reinterpret_cast<const short *>(n + 0xE);
    if (profile_index < 0 || static_cast<unsigned>(profile_index) >= *reinterpret_cast<const unsigned *>(kNumProfiles)) return 0;
    const unsigned char *profile = profiles + profile_index * kProfileSize;
    const int lanes = std::min<int>(profile[0], 15);
    const int mid = inverted ? profile[0] - profile[1] : profile[1];
    if (middle) *middle = mid;
    for (int lane = 0; lane < lanes; ++lane) {
        const int number = inverted ? profile[0] - lane - 1 : lane;
        const unsigned bits = *reinterpret_cast<const unsigned *>(profile + 4 + 4 * (number < 15 ? number : 0));
        const float width = static_cast<float>(static_cast<int>(bits << 14) >> 18) * (100.0f / 8191.0f);
        const float offset = static_cast<float>(static_cast<int>(bits) >> 18) * (100.0f / 8191.0f);
        const float centre = lane < mid ? -offset : offset;
        out[lane] = {static_cast<int>(bits & 15), centre - 0.5f * width, centre + 0.5f * width};
    }
    return lanes;
}

}  // namespace nfsmw::road

// The game's road network as the PC build lays it out (agent_docs/ai-driving.md): nodes (0x20 bytes, [0x9B38BC]), segments (0x16 bytes,
// [0x9B38C0]) and road profiles (0x40 bytes, [0x9B38B8]). Read-only helpers that check what they read.
#pragma once

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace nfsmw::road {

constexpr uintptr_t kNodes = 0x9B38BC, kSegments = 0x9B38C0, kProfiles = 0x9B38B8, kRoads = 0x9B38C8;
constexpr uintptr_t kNumSegments = 0x9B3874, kNumNodes = 0x9B387C, kNumProfiles = 0x9B3884;  // WRoadNetwork::fNumSegments / fNumNodes
constexpr unsigned kNodeSize = 0x20, kSegmentSize = 0x16, kProfileSize = 0x40, kRoadSize = 8;
constexpr unsigned kRacingDrivableLanes = 0xFFFFDF7F;  // drivable_lanes[kLaneRacing]: bit n = lane type n is drivable

inline bool readable(const void *p, size_t n) { return p && !IsBadReadPtr(p, n); }

inline unsigned node_count() { return *reinterpret_cast<const unsigned *>(kNumNodes); }
inline unsigned segment_count() { return *reinterpret_cast<const unsigned *>(kNumSegments); }
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
    const int zones = profile[0];
    float width = 0.0f;
    size_t used = 0;
    for (int i = 0; i < zones && i < 15; ++i) {
        const unsigned bits = *reinterpret_cast<const unsigned *>(profile + 4 + 4 * i);
        const unsigned type = bits & 15;
        const int raw = static_cast<int>(bits << 14) >> 18;  // 14 bits from bit 4, signed
        const float lane_width = static_cast<float>(raw) * (100.0f / 8191.0f);
        if ((kRacingDrivableLanes >> type) & 1) width += lane_width;
        if (lanes_text && used + 16 < lanes_size) used += snprintf(lanes_text + used, lanes_size - used, "%u:%.1f ", type, lane_width);
    }
    return width;
}

// A segment's drivable width: the narrower of its two ends.
inline float segment_width(const unsigned char *segment) {
    const float a = drivable_width(segment_node(segment, 0)), b = drivable_width(segment_node(segment, 1));
    return a < b ? a : b;
}

}  // namespace nfsmw::road

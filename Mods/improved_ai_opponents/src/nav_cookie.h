// The cookie trail of a WRoadNav: the road's cross-sections between the car and the nav's head, which is the corridor the car steers through
// (agent_docs/ai-driving.md, "The corridor"). Read with checks: a trail that is not there or not readable is an empty one.
#pragma once

#include <windows.h>

#include <cmath>

#include "road_network.h"

namespace improved_ai_opponents::nav_cookie {

// A NavCookie (0x40 bytes, the decomp's layout is the PC's): a cross-section of the road the nav has walked along; the positions of its edges are (x, z). `left_offset`
// and `right_offset` are the distances of the road's edges from `centre` across `forward`.
struct Cookie {
    float left[2], right[2], forward[2];
    float length, curvature, left_offset, right_offset;
    unsigned flags;
    float pad;
    float centre[3];
    short segment_parameter;
    unsigned short segment;

    // How far a point is across the road from this cookie's centre, positive towards the right edge (the game's `current_offset`: bCross(cookie_to_point, forward)).
    float across(float x, float z) const { return (x - centre[0]) * forward[1] - (z - centre[2]) * forward[0]; }
};
static_assert(sizeof(Cookie) == 0x40, "NavCookie");

constexpr unsigned kNavTrail = 0x08, kNavHasTrail = 0x55;  // WRoadNav, PC offsets
constexpr int kTrailSize = 32;

class Trail {
  public:
    explicit Trail(const void *nav) {
        const unsigned char *n = static_cast<const unsigned char *>(nav);
        if (!n || !n[kNavHasTrail]) return;
        const unsigned char *trail = *reinterpret_cast<unsigned char *const *>(n + kNavTrail);
        if (!nfsmw::road::readable(trail, 0x10 + kTrailSize * sizeof(Cookie))) return;
        const int count = *reinterpret_cast<const int *>(trail), last = *reinterpret_cast<const int *>(trail + 4);
        if (count < 2 || count > kTrailSize) return;
        count_ = count;
        last_ = last;
        cookies_ = reinterpret_cast<const Cookie *>(trail + 0x10);
    }
    int size() const { return count_; }
    const Cookie &operator[](int n) const {  // the n-th oldest
        return cookies_[count_ < kTrailSize ? n % count_ : (last_ + 1 + n) % kTrailSize];
    }
    // The index of the cookie whose centre is nearest to a point in the ground plane (-1 for an empty trail).
    int nearest(float x, float z) const {
        int best = -1;
        float best_distance = 1e30f;
        for (int n = 0; n < count_; ++n) {
            const float d = std::hypot((*this)[n].centre[0] - x, (*this)[n].centre[2] - z);
            if (d < best_distance) {
                best_distance = d;
                best = n;
            }
        }
        return best;
    }

  private:
    int count_ = 0, last_ = 0;
    const Cookie *cookies_ = nullptr;
};

}  // namespace improved_ai_opponents::nav_cookie

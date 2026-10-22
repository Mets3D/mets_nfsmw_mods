// A road segment drawn into a picture (lines.h): its corridor over the drivable width, or the lanes of its profile coloured by type, curved as the road is.
// Used for the route of the AI (overlay.cpp) and for the segments of the road inspector (road_inspector.cpp).
#include "segment_draw.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "road_geometry.h"
#include "road_network.h"

namespace improved_ai_opponents::segment_draw {

using canvas::Lines;
using geometry::bezier_point;
using geometry::segment_geometry;
using geometry::SegmentGeometry;
using geometry::side_curve;
namespace road = nfsmw::road;

namespace {

// How many pieces the part of the segment from `t_from` to `t_to` (its curve's parameter) is drawn in; 0 when it is nothing.
int steps_of(const SegmentGeometry &g, float t_from, float t_to) {
    Vec3 middle[4];
    side_curve(g, 0.0f, 0.0f, middle);
    const float length = std::hypot(middle[1].x - middle[0].x, middle[1].z - middle[0].z) + std::hypot(middle[2].x - middle[1].x, middle[2].z - middle[1].z) +
                         std::hypot(middle[3].x - middle[2].x, middle[3].z - middle[2].z);
    const float share = t_to - t_from;
    if (length * share < 0.5f) return 0;
    return std::max(1, static_cast<int>(std::ceil(static_cast<float>(std::clamp(static_cast<int>(length / 5.0f) + 1, g.curved ? 8 : 2, 24)) * share)));
}

}  // namespace

// One road segment as a corridor: the road's left and right side over the drivable width. A segment with no drivable width is a plain line.
void add_corridor(Lines &lines, unsigned index, DWORD edge, DWORD fill, float t_from, float t_to) {
    SegmentGeometry g;
    if (!segment_geometry(index, &g)) return;
    const float wa = 0.5f * road::drivable_width(g.a), wb = 0.5f * road::drivable_width(g.b);
    if (wa < 0.25f && wb < 0.25f) {
        lines.add({g.node_a.x, g.node_a.y + 0.3f, g.node_a.z}, {g.node_b.x, g.node_b.y + 0.3f, g.node_b.z}, edge);
        return;
    }
    const int steps = steps_of(g, t_from, t_to);
    if (steps == 0) return;
    Vec3 curve[3][4];  // the left side, the middle and the right side
    for (int side = 0; side < 3; ++side) side_curve(g, wa * static_cast<float>(side - 1), wb * static_cast<float>(side - 1), curve[side]);
    Vec3 last[3]{};
    for (int k = 0; k <= steps; ++k) {
        const float t = t_from + (t_to - t_from) * static_cast<float>(k) / static_cast<float>(steps);
        Vec3 now[3];
        for (int side = 0; side < 3; ++side) {
            now[side] = bezier_point(curve[side], t);
            now[side].y += 0.3f;
        }
        if (k > 0) {
            lines.add(last[0], now[0], edge);
            lines.add(last[2], now[2], edge);
            if (fill) {  // strips of two shades, so that the way the road bends shows
                lines.add(last[1], now[1], edge);
                const DWORD shade = (k & 1) ? fill : (fill & 0x00FFFFFF) | (((fill >> 24) * 3 / 5) << 24);
                lines.add_quad(last[0], last[2], now[2], now[0], shade);
            }
        }
        if (k == 0 || k == steps || fill) lines.add(now[0], now[2], edge);
        for (int side = 0; side < 3; ++side) last[side] = now[side];
    }
}

// The same segment as the lanes of its profile, each a strip coloured by its type: 2 (sidewalks, medians, shoulders: drivable but rough) yellow, 7 and 13 (lanes
// the AI avoids because they lead into obstacles) red, any other type (the road) green. With as many lanes at both ends a lane is joined to the lane with the same
// number at the other end. A segment between profiles with different numbers of lanes (a narrow path that becomes a highway) has no lanes to join: the span across
// the road is cut at where a lane begins at either end, at the same share of the way across, and a strip takes the type of the lane at the end it is nearer to.
// (The game does not join lanes either: each end's corridor limits are made on their own and joined by a spline.)
void add_lanes(Lines &lines, unsigned index, float t_from, float t_to) {
    SegmentGeometry g;
    if (!segment_geometry(index, &g)) return;
    road::Lane la[15], lb[15];
    const int na = road::profile_lanes(g.a, (g.flags & road::kFlagStartInverted) != 0, la), nb = road::profile_lanes(g.b, (g.flags & road::kFlagEndInverted) != 0, lb);
    const int steps = steps_of(g, t_from, t_to);
    if (na == 0 || nb == 0 || steps == 0) return;
    struct Strip {
        float from_a, to_a, from_b, to_b;
        int type_a, type_b;
    };
    std::vector<Strip> strips;
    if (na == nb) {
        for (int i = 0; i < na; ++i) strips.push_back({la[i].from, la[i].to, lb[i].from, lb[i].to, la[i].type, lb[i].type});
    } else {
        const float start_a = la[0].from, span_a = std::max(0.01f, la[na - 1].to - la[0].from), start_b = lb[0].from, span_b = std::max(0.01f, lb[nb - 1].to - lb[0].from);
        std::vector<float> cuts = {0.0f, 1.0f};
        for (int i = 1; i < na; ++i) cuts.push_back((la[i].from - start_a) / span_a);
        for (int i = 1; i < nb; ++i) cuts.push_back((lb[i].from - start_b) / span_b);
        std::sort(cuts.begin(), cuts.end());
        const auto type_at = [](const road::Lane *lanes, int count, float start, float span, float share) {
            for (int i = 0; i < count; ++i)
                if (start + share * span < lanes[i].to) return lanes[i].type;
            return lanes[count - 1].type;
        };
        for (size_t i = 0; i + 1 < cuts.size(); ++i) {
            if (cuts[i + 1] - cuts[i] < 0.002f) continue;
            const float mid = 0.5f * (cuts[i] + cuts[i + 1]);
            strips.push_back({start_a + cuts[i] * span_a, start_a + cuts[i + 1] * span_a, start_b + cuts[i] * span_b, start_b + cuts[i + 1] * span_b,
                              type_at(la, na, start_a, span_a, mid), type_at(lb, nb, start_b, span_b, mid)});
        }
    }
    const auto color_of = [](int type) { return type == 2 ? 0xFFF0D030u : (type == 7 || type == 13) ? 0xFFF03030u : 0xFF40D040u; };
    for (const Strip &strip : strips) {
        Vec3 left[4], right[4];
        side_curve(g, strip.from_a, strip.from_b, left);
        side_curve(g, strip.to_a, strip.to_b, right);
        Vec3 last_left{}, last_right{};
        for (int k = 0; k <= steps; ++k) {
            const float t = t_from + (t_to - t_from) * static_cast<float>(k) / static_cast<float>(steps);
            Vec3 l = bezier_point(left, t), r = bezier_point(right, t);
            l.y += 0.3f;
            r.y += 0.3f;
            if (k > 0) {
                const DWORD color = color_of(2 * k <= steps ? strip.type_a : strip.type_b);
                lines.add(last_left, l, color);
                lines.add(last_right, r, color);
                lines.add_quad(last_left, last_right, r, l, (color & 0x00FFFFFF) | 0x58000000);
            }
            last_left = l;
            last_right = r;
        }
    }
}

}  // namespace improved_ai_opponents::segment_draw

// The shape of a road segment, the way the game builds it (agent_docs/ai-driving.md, "Segment curves"): the middle curve and the sides of a corridor as cubic
// Beziers, and how fast a car can follow the middle curve.
#pragma once

#include "vec3.h"

namespace improved_ai_opponents::geometry {

// A road segment's geometry the way the game builds the sides of a nav's corridor (WRoadNav::SetBoundPos / SetControlPos): a side runs from a point beside the
// segment's start node to one beside its end node, the points being the node's position plus the segment's right vector at that end (perpendicular to the end's
// handle) times an offset; a curved segment's side is a cubic Bezier with the segment's own control vectors (scaled by how long the side is against the distance
// between the nodes). Not the middle curve moved sideways, which folds over on a tight bend.
struct SegmentGeometry {
    unsigned a = 0, b = 0;  // the nodes
    unsigned flags = 0;
    Vec3 node_a{}, node_b{}, right_start{}, right_end{}, start_control{}, end_control{};
    float between = 1.0f;  // the distance between the nodes
    bool curved = false;
};

bool segment_geometry(unsigned index, SegmentGeometry *g);

// The control points of the side that is `offset_a` from the start node and `offset_b` from the end node along the right vectors (0 and 0: the middle).
void side_curve(const SegmentGeometry &g, float offset_a, float offset_b, Vec3 (&c)[4]);

// The length (metres, 3D) of the middle curve: what the game stores as the segment's length, to compare with it.
float curve_length(const SegmentGeometry &g);

Vec3 bezier_point(const Vec3 (&p)[4], float t);

// The point of the segment's middle curve nearest to `point` (3D): its parameter `t` (0 to 1, as the game's FindClosestOnSpline gives it), the point and the distance.
struct Nearest {
    float t, distance;
    Vec3 position;
};
Nearest nearest_on_curve(const SegmentGeometry &g, const float point[3]);

// How fast a car can drive a segment: sqrt(lateral_accel / curvature) at the tightest stretch of the best line through the segment's lanes, at most
// `top_speed`. The best line is the flattest one that stays inside a run of the lanes a racing car may drive on (the rough lanes, type 2, between road lanes
// included, those at the outer edge for 1.5 m only), found by pulling a line through the road tight: its ends may be anywhere across the road but it leaves and arrives in the direction the road goes (a smooth chain through a node with two segments: the chords' direction, else the road's tangent), and its points are on chords across the real road (between the game's side curves), so a wide road that bends gently is a straight but a corner is a corner.
// The curvature is that of the line in the horizontal plane, averaged over 12 m so that a spike does not count. `radius` is the best line's tightest radius,
// `centre_radius` the middle curve's (metres; 1e9 for a straight line). What happens where two segments meet is not in this (the angle between them).
struct CornerLimit {
    float radius;
    float centre_radius;
    float tightest_t;  // where along the segment (the curve's parameter, 0 to 1) the best line is at its tightest
};
CornerLimit corner_limit(unsigned segment_index);

// What the solver worked with for a segment, to draw it (worked out again, not cached; only for a segment that has the curved flag).
struct SolverView {
    static constexpr int kCount = 25;  // the samples along the segment, an equal distance apart (in the horizontal plane)
    Vec3 control[4];                   // the Bezier control points of the middle curve
    Vec3 centre[kCount], best[kCount], left[kCount], right[kCount];  // the middle curve, the best line, and the edges of the band the best line may be in
    float centre_curvature[kCount], best_curvature[kCount];          // 1/m at each sample, averaged over 12 m: what the limit speed is made from
    bool has_band;     // false: the road gives no band (no lanes), the best line is the middle curve
    bool used_middle;  // the solved line was tighter than the middle curve, so the middle curve is the best line
    float band_start, band_end;  // how wide the band is at the two ends (metres)
};
bool solver_view(unsigned segment_index, SolverView *view);
// Starts working the limit of every segment out in a thread of its own (once for each network that is loaded: the route search asks for thousands).
void precompute_limits();

}  // namespace improved_ai_opponents::geometry

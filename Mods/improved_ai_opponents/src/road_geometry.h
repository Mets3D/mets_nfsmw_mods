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

Vec3 bezier_point(const Vec3 (&p)[4], float t);

// The point of the segment's middle curve nearest to `point` (3D): its parameter `t` (0 to 1, as the game's FindClosestOnSpline gives it), the point and the distance.
struct Nearest {
    float t, distance;
    Vec3 position;
};
Nearest nearest_on_curve(const SegmentGeometry &g, const float point[3]);

// How fast a car can drive a segment: sqrt(lateral_accel / curvature) at the tightest stretch of the best line through the segment's lanes, at most
// `top_speed`. The best line is the flattest one that stays inside a run of the lanes a racing car may drive on (the rough lanes, type 2, between road lanes
// included, those at the outer edge for 1.5 m only), less a margin of 2.5 m at each edge (the game's own: half a car and 1.5 m), found by pulling a line through the road tight with its ends free to be anywhere across the road: a wide road that bends gently is a straight.
// The curvature is that of the line in the horizontal plane, averaged over 12 m so that a spike does not count. `radius` is the best line's tightest radius,
// `centre_radius` the middle curve's (metres; 1e9 for a straight line). What happens where two segments meet is not in this (the angle between them).
struct CornerLimit {
    float radius;
    float centre_radius;
};
CornerLimit corner_limit(unsigned segment_index);
// Starts working the limit of every segment out in a thread of its own (once for each network that is loaded: the route search asks for thousands).
void precompute_limits();

}  // namespace improved_ai_opponents::geometry

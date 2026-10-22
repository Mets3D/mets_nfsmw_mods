#include "road_geometry.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "improved_ai_opponents.h"

namespace improved_ai_opponents::geometry {

bool segment_geometry(unsigned index, SegmentGeometry *g) {
    constexpr float kHandleScale = 500.0f / (127.0f * 65535.0f);
    const unsigned char *s = road::segment(index);
    if (!s) return false;
    g->a = road::segment_node(s, 0);
    g->b = road::segment_node(s, 1);
    g->flags = road::segment_flags(s);
    const float *pa = road::node_position(g->a), *pb = road::node_position(g->b);
    if (!pa || !pb) return false;
    const signed char *end_handle = reinterpret_cast<const signed char *>(s + 0x10), *start_handle = reinterpret_cast<const signed char *>(s + 0x13);
    // WRoadSegment::GetStartRightVec / GetEndRightVec
    g->right_start = {start_handle[2] / 127.0f, 0.0f, -start_handle[0] / 127.0f};
    g->right_end = {-end_handle[2] / 127.0f, 0.0f, end_handle[0] / 127.0f};
    g->curved = (g->flags & road::kFlagCurved) != 0;
    if (g->curved) {
        const float end_scale = *reinterpret_cast<const unsigned short *>(s + 0xC) * kHandleScale, start_scale = *reinterpret_cast<const unsigned short *>(s + 0xE) * kHandleScale;
        g->start_control = {start_scale * start_handle[0], start_scale * start_handle[1], start_scale * start_handle[2]};
        g->end_control = {end_scale * end_handle[0], end_scale * end_handle[1], end_scale * end_handle[2]};
    }
    g->node_a = {pa[0], pa[1], pa[2]};
    g->node_b = {pb[0], pb[1], pb[2]};
    g->between = std::max(0.01f, std::sqrt((pb[0] - pa[0]) * (pb[0] - pa[0]) + (pb[1] - pa[1]) * (pb[1] - pa[1]) + (pb[2] - pa[2]) * (pb[2] - pa[2])));
    return true;
}

void side_curve(const SegmentGeometry &g, float offset_a, float offset_b, Vec3 (&c)[4]) {
    const Vec3 p0 = {g.node_a.x + g.right_start.x * offset_a, g.node_a.y + g.right_start.y * offset_a, g.node_a.z + g.right_start.z * offset_a};
    const Vec3 p3 = {g.node_b.x + g.right_end.x * offset_b, g.node_b.y + g.right_end.y * offset_b, g.node_b.z + g.right_end.z * offset_b};
    const float scale = std::max(0.01f, std::sqrt((p3.x - p0.x) * (p3.x - p0.x) + (p3.y - p0.y) * (p3.y - p0.y) + (p3.z - p0.z) * (p3.z - p0.z))) / g.between;
    c[0] = p0;
    c[1] = {p0.x + g.start_control.x * scale, p0.y + g.start_control.y * scale, p0.z + g.start_control.z * scale};
    c[2] = {p3.x + g.end_control.x * scale, p3.y + g.end_control.y * scale, p3.z + g.end_control.z * scale};
    c[3] = p3;
}

Vec3 bezier_point(const Vec3 (&p)[4], float t) {
    const float u = 1.0f - t, a = u * u * u, b = 3.0f * u * u * t, c = 3.0f * u * t * t, d = t * t * t;
    return {a * p[0].x + b * p[1].x + c * p[2].x + d * p[3].x, a * p[0].y + b * p[1].y + c * p[2].y + d * p[3].y, a * p[0].z + b * p[1].z + c * p[2].z + d * p[3].z};
}

float curve_length(const SegmentGeometry &g) {
    Vec3 c[4];
    side_curve(g, 0.0f, 0.0f, c);
    constexpr int kSteps = 64;
    float length = 0.0f;
    Vec3 previous = bezier_point(c, 0.0f);
    for (int k = 1; k <= kSteps; ++k) {
        const Vec3 p = bezier_point(c, static_cast<float>(k) / kSteps);
        length += std::sqrt((p.x - previous.x) * (p.x - previous.x) + (p.y - previous.y) * (p.y - previous.y) + (p.z - previous.z) * (p.z - previous.z));
        previous = p;
    }
    return length;
}

Nearest nearest_on_curve(const SegmentGeometry &g, const float point[3]) {
    Vec3 c[4];
    side_curve(g, 0.0f, 0.0f, c);
    const auto distance_at = [&](float t, Vec3 *at) {
        const Vec3 p = bezier_point(c, t);
        if (at) *at = p;
        return std::sqrt((p.x - point[0]) * (p.x - point[0]) + (p.y - point[1]) * (p.y - point[1]) + (p.z - point[2]) * (p.z - point[2]));
    };
    constexpr int kSamples = 24;
    int best = 0;
    float best_distance = 1e30f;
    for (int i = 0; i <= kSamples; ++i) {
        const float d = distance_at(static_cast<float>(i) / kSamples, nullptr);
        if (d < best_distance) {
            best_distance = d;
            best = i;
        }
    }
    float lo = std::max(0.0f, static_cast<float>(best - 1) / kSamples), hi = std::min(1.0f, static_cast<float>(best + 1) / kSamples);
    for (int i = 0; i < 12; ++i) {  // narrowed down between the samples either side of the nearest
        const float a = lo + (hi - lo) / 3.0f, b = hi - (hi - lo) / 3.0f;
        if (distance_at(a, nullptr) < distance_at(b, nullptr)) hi = b;
        else lo = a;
    }
    Nearest n;
    n.t = 0.5f * (lo + hi);
    n.distance = distance_at(n.t, &n.position);
    return n;
}

namespace {

constexpr float kStraight = 1e9f, kWindow = 12.0f, kRoughDepth = 1.5f;
constexpr int kSamples = 24;
constexpr double kHeadingWeight = 4.0;  // how much the heading at the ends of the line counts against its curvature
constexpr int kPasses = 5;               // how many times the line is solved again with its own sideways direction

Mutex g_lock;

// The curvature along a polyline in the horizontal plane (the circle through each three neighbouring points), averaged over about kWindow metres; the tightest.
float tightest_curvature(const float (&x)[kSamples + 1], const float (&z)[kSamples + 1], int *at, float *per_sample = nullptr) {
    float length = 0.0f;
    for (int i = 1; i <= kSamples; ++i) length += std::hypot(x[i] - x[i - 1], z[i] - z[i - 1]);
    *at = 0;
    if (per_sample) std::fill(per_sample, per_sample + kSamples + 1, 0.0f);
    if (length < 0.5f) return 0.0f;
    float curvature[kSamples + 1] = {};
    for (int i = 1; i < kSamples; ++i) {
        const float ax = x[i] - x[i - 1], az = z[i] - z[i - 1], bx = x[i + 1] - x[i], bz = z[i + 1] - z[i], cx = x[i + 1] - x[i - 1], cz = z[i + 1] - z[i - 1];
        const float denominator = std::hypot(ax, az) * std::hypot(bx, bz) * std::hypot(cx, cz);
        curvature[i] = denominator > 1e-6f ? 2.0f * std::fabs(ax * bz - az * bx) / denominator : 0.0f;
    }
    curvature[0] = curvature[1];
    curvature[kSamples] = curvature[kSamples - 1];
    const int half = std::max(0, static_cast<int>(std::lround(0.5f * kWindow / (length / kSamples))));
    float tightest = 0.0f;
    for (int i = 0; i <= kSamples; ++i) {
        const int from = std::max(0, i - half), to = std::min(kSamples, i + half);
        float sum = 0.0f;
        for (int k = from; k <= to; ++k) sum += curvature[k];
        const float average = sum / static_cast<float>(to - from + 1);
        if (per_sample) per_sample[i] = average;
        if (average > tightest) {
            tightest = average;
            *at = i;
        }
    }
    return tightest;
}

// A run of neighbouring lanes a racing car may drive on, as a span across the road (along the right vector, from the node).
struct Run {
    float from, to;
    float centre() const { return 0.5f * (from + to); }
    float width() const { return to - from; }
};

// The runs of lanes at one end of a segment: every lane a racing car may drive on counts (the rough lanes, type 2, between road lanes are crossed: a median is
// drivable), only the lanes that cannot be driven (7, 13) end a run, and a rough lane at the outer edge of a run counts for kRoughDepth metres of its width, wheels
// on the kerb (a run of nothing but rough lanes is taken whole).
int drivable_runs(unsigned node, bool inverted, Run out[8]) {
    road::Lane lanes[15];
    const int count = road::profile_lanes(node, inverted, lanes);
    int runs = 0;
    int i = 0;
    while (i < count && runs < 8) {
        if (!((road::kRacingDrivableLanes >> lanes[i].type) & 1)) {
            ++i;
            continue;
        }
        const int first = i;
        bool any_road = false;
        for (; i < count && ((road::kRacingDrivableLanes >> lanes[i].type) & 1); ++i) any_road = any_road || ((road::kRoadLanes >> lanes[i].type) & 1);
        const int last = i - 1;
        Run run = {lanes[first].from, lanes[last].to};
        if (any_road) {
            if ((road::kRoughLanes >> lanes[first].type) & 1) run.from = lanes[first].to - std::min(kRoughDepth, lanes[first].to - lanes[first].from);
            if ((road::kRoughLanes >> lanes[last].type) & 1) run.to = lanes[last].from + std::min(kRoughDepth, lanes[last].to - lanes[last].from);
        }
        out[runs++] = run;
    }
    return runs;
}

// The run at each end that a line would join: the pair whose middles are nearest (a little less for wide ones). False when an end has no run.
bool pick_runs(unsigned a, bool inverted_a, unsigned b, bool inverted_b, Run *run_a, Run *run_b) {
    Run ra[8], rb[8];
    const int na = drivable_runs(a, inverted_a, ra), nb = drivable_runs(b, inverted_b, rb);
    if (na == 0 && nb == 0) return false;
    if (na == 0 || nb == 0) {
        *run_a = *run_b = na ? ra[0] : rb[0];
        return true;
    }
    float best = 1e30f;
    for (int i = 0; i < na; ++i) {
        for (int j = 0; j < nb; ++j) {
            const float score = std::fabs(ra[i].centre() - rb[j].centre()) - 0.25f * std::min(ra[i].width(), rb[j].width());
            if (score < best) {
                best = score;
                *run_a = ra[i];
                *run_b = rb[j];
            }
        }
    }
    return true;
}

// The direction a car travels through an end of a segment when the road goes smoothly on there: at a node where just two segments meet, the two chords added (the
// line from the far end of the other segment to the far end of this one), so that the wiggle of one segment's own curve does not turn the car. False at a junction, or
// where the two chords turn more than 45 degrees: the road's own tangent then.
bool chain_heading(unsigned segment_index, const SegmentGeometry &g, bool at_start, float *hx, float *hz) {
    const unsigned node = at_start ? g.a : g.b;
    const unsigned char *n = road::node(node);
    if (!n || road::node_segment_count(n) != 2) return false;
    const unsigned first = road::node_segment(n, 0), second = road::node_segment(n, 1);
    const unsigned other = first == segment_index ? second : first;
    const unsigned char *s = road::segment(other);
    const float *far_end = s ? road::node_position(road::other_node(s, node)) : nullptr;
    if (!far_end || other == segment_index) return false;
    const Vec3 &here = at_start ? g.node_a : g.node_b, &there = at_start ? g.node_b : g.node_a;
    float ax = here.x - far_end[0], az = here.z - far_end[2];  // arriving at the node from the other segment
    float bx = there.x - here.x, bz = there.z - here.z;       // leaving along this segment
    if (!at_start) {  // at the end of the segment the car arrives along it and leaves along the other
        ax = here.x - there.x;
        az = here.z - there.z;
        bx = far_end[0] - here.x;
        bz = far_end[2] - here.z;
    }
    const float la = std::hypot(ax, az), lb = std::hypot(bx, bz);
    if (la < 1e-3f || lb < 1e-3f || (ax * bx + az * bz) / (la * lb) < 0.7f) return false;
    const float x = ax / la + bx / lb, z = az / la + bz / lb, l = std::hypot(x, z);
    if (l < 1e-3f) return false;
    *hx = x / l;
    *hz = z / l;
    return true;
}

// Solves A x = b for n unknowns (Gaussian elimination with pivoting; a singular column leaves its unknown 0).
void solve_dense(double a[kSamples + 1][kSamples + 1], double b[kSamples + 1], int n) {
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int row = col + 1; row < n; ++row)
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
        if (std::fabs(a[pivot][col]) < 1e-12) continue;
        if (pivot != col) {
            for (int k = 0; k < n; ++k) std::swap(a[col][k], a[pivot][k]);
            std::swap(b[col], b[pivot]);
        }
        for (int row = col + 1; row < n; ++row) {
            const double f = a[row][col] / a[col][col];
            for (int k = col; k < n; ++k) a[row][k] -= f * a[col][k];
            b[row] -= f * b[col];
        }
    }
    for (int row = n - 1; row >= 0; --row) {
        double sum = b[row];
        for (int k = row + 1; k < n; ++k) sum -= a[row][k] * b[k];
        b[row] = std::fabs(a[row][row]) < 1e-12 ? 0.0 : sum / a[row][row];
    }
}

// Minimizes 1/2 u M u - b u with every u[i] between lo[i] and hi[i], from the feasible start in `u` (a primal active set method: the variables at a bound are held
// there, the others are solved for, a step that would cross a bound stops at it and holds that variable, and a held variable whose bound pulls the wrong way is let
// go again). Holding every variable that ends up outside at once, and never letting any go, makes a line that clings to the edges of a band and jumps across.
void solve_box(const double (&m)[kSamples + 1][kSamples + 1], const double (&b)[kSamples + 1], const double (&lo)[kSamples + 1], const double (&hi)[kSamples + 1], double (&u)[kSamples + 1]) {
    constexpr int n = kSamples + 1;
    int state[n] = {};  // 0 free, -1 held at the lower bound, 1 at the upper
    for (int step = 0; step < 4 * n; ++step) {
        int free_index[n], free_count = 0;
        for (int i = 0; i < n; ++i)
            if (state[i] == 0) free_index[free_count++] = i;
        double work_matrix[n][n], work_rhs[n];
        for (int a = 0; a < free_count; ++a) {
            double sum = b[free_index[a]];
            for (int k = 0; k < n; ++k)
                if (state[k] != 0) sum -= m[free_index[a]][k] * u[k];
            work_rhs[a] = sum;
            for (int k = 0; k < free_count; ++k) work_matrix[a][k] = m[free_index[a]][free_index[k]];
        }
        solve_dense(work_matrix, work_rhs, free_count);
        // as far towards the solution as the bounds allow
        double alpha = 1.0;
        int blocking = -1, side = 0;
        for (int a = 0; a < free_count; ++a) {
            const int i = free_index[a];
            const double d = work_rhs[a] - u[i];
            if (d < -1e-12 && u[i] + d < lo[i] && (lo[i] - u[i]) / d < alpha) {
                alpha = (lo[i] - u[i]) / d;
                blocking = i;
                side = -1;
            } else if (d > 1e-12 && u[i] + d > hi[i] && (hi[i] - u[i]) / d < alpha) {
                alpha = (hi[i] - u[i]) / d;
                blocking = i;
                side = 1;
            }
        }
        for (int a = 0; a < free_count; ++a) u[free_index[a]] += alpha * (work_rhs[a] - u[free_index[a]]);
        if (blocking >= 0) {
            u[blocking] = side < 0 ? lo[blocking] : hi[blocking];
            state[blocking] = side;
            continue;
        }
        // at the solution of the free ones: does a held one want to move away from its bound?
        int release = -1;
        double worst = 1e-9;
        for (int i = 0; i < n; ++i) {
            if (state[i] == 0) continue;
            double gradient = -b[i];
            for (int k = 0; k < n; ++k) gradient += m[i][k] * u[k];
            const double wrong = state[i] < 0 ? -gradient : gradient;  // at the lower bound a negative gradient wants more, at the upper a positive one wants less
            if (wrong > worst) {
                worst = wrong;
                release = i;
            }
        }
        if (release < 0) return;
        state[release] = 0;
    }
}

CornerLimit work_out_limit(unsigned segment_index, SolverView *view = nullptr) {
    SegmentGeometry g;
    if (!segment_geometry(segment_index, &g) || !g.curved) return {kStraight, kStraight, 0.5f};
    Vec3 c[4];
    side_curve(g, 0.0f, 0.0f, c);
    // The middle curve at points an equal distance apart (in the horizontal plane), and the right vector there (the tangent turned a quarter, as the game's
    // GetStartRightVec / GetEndRightVec do)
    constexpr int kFine = 240;
    float cumulative[kFine + 1];
    cumulative[0] = 0.0f;
    Vec3 previous = bezier_point(c, 0.0f);
    for (int k = 1; k <= kFine; ++k) {
        const Vec3 p = bezier_point(c, static_cast<float>(k) / kFine);
        cumulative[k] = cumulative[k - 1] + std::hypot(p.x - previous.x, p.z - previous.z);
        previous = p;
    }
    if (cumulative[kFine] < 0.5f) return {kStraight, kStraight, 0.5f};
    float ts[kSamples + 1];
    ts[0] = 0.0f;
    ts[kSamples] = 1.0f;
    for (int i = 1, k = 1; i < kSamples; ++i) {
        const float target = cumulative[kFine] * static_cast<float>(i) / kSamples;
        while (k < kFine && cumulative[k] < target) ++k;
        ts[i] = (static_cast<float>(k - 1) + (target - cumulative[k - 1]) / std::max(1e-6f, cumulative[k] - cumulative[k - 1])) / kFine;
    }
    float cx[kSamples + 1], cy[kSamples + 1], cz[kSamples + 1], rx[kSamples + 1], rz[kSamples + 1];
    for (int i = 0; i <= kSamples; ++i) {
        const float t = ts[i], u = 1.0f - t;
        const Vec3 p = bezier_point(c, t);
        const float tx = 3.0f * (u * u * (c[1].x - c[0].x) + 2.0f * u * t * (c[2].x - c[1].x) + t * t * (c[3].x - c[2].x));
        const float tz = 3.0f * (u * u * (c[1].z - c[0].z) + 2.0f * u * t * (c[2].z - c[1].z) + t * t * (c[3].z - c[2].z));
        const float l = std::max(1e-4f, std::hypot(tx, tz));
        cx[i] = p.x;
        cy[i] = p.y;
        cz[i] = p.z;
        rx[i] = tz / l;
        rz[i] = -tx / l;
    }
    int centre_at = 0;
    float centre_per_sample[kSamples + 1];
    const float centre = tightest_curvature(cx, cz, &centre_at, centre_per_sample);
    if (view) {
        for (int k = 0; k < 4; ++k) view->control[k] = c[k];
        for (int i = 0; i <= kSamples; ++i) {
            view->centre[i] = view->best[i] = {cx[i], cy[i], cz[i]};
            view->centre_curvature[i] = view->best_curvature[i] = centre_per_sample[i];
        }
        view->has_band = false;
        view->used_middle = false;
        view->band_start = view->band_end = 0.0f;
    }
    const float centre_radius = centre < 1e-6f ? kStraight : 1.0f / centre;
    // The band across the road at each sample: the run of lanes at each end that the line joins, joined (the whole road: the line's centre may go to its edge)
    Run run_a, run_b;
    if (!pick_runs(g.a, (g.flags & road::kFlagStartInverted) != 0, g.b, (g.flags & road::kFlagEndInverted) != 0, &run_a, &run_b))
        return {centre_radius, centre_radius, ts[centre_at]};
    constexpr int n = kSamples + 1;
    // The road's own edges: the left and right edge of the band are the game's side curves (the segment's Bezier moved to the offsets of the band at each end, with
    // the control vectors scaled for the longer or shorter side, which is how the game builds a road's side), not the middle curve moved sideways: that folds over
    // on a tight bend and then lies outside the road. At each sample the line is a point across the road between the two edges, u = 0 at the left edge and 1 at the right.
    Vec3 left_curve[4], right_curve[4];
    side_curve(g, run_a.from, run_b.from, left_curve);
    side_curve(g, run_a.to, run_b.to, right_curve);
    // Across the heading at the two ends: the road's tangent (the middle curve's right vector), or the smooth chain's where the road goes on through the node
    float start_nx = rx[0], start_nz = rz[0], end_nx = rx[kSamples], end_nz = rz[kSamples], hx = 0.0f, hz = 0.0f;
    if (chain_heading(segment_index, g, true, &hx, &hz)) {
        start_nx = hz;
        start_nz = -hx;
    }
    if (chain_heading(segment_index, g, false, &hx, &hz)) {
        end_nx = hz;
        end_nz = -hx;
    }
    float bx[n], bz[n], vx[n], vz[n];  // the left edge's point and the vector across the road to the right edge's
    double lo[n], hi[n];
    for (int i = 0; i < n; ++i) {
        const Vec3 l = bezier_point(left_curve, ts[i]), r = bezier_point(right_curve, ts[i]);
        bx[i] = l.x;
        bz[i] = l.z;
        vx[i] = r.x - l.x;
        vz[i] = r.z - l.z;
        lo[i] = 0.0;
        hi[i] = 1.0;
        if (view) {
            view->left[i] = l;
            view->right[i] = r;
        }
    }
    if (view) {
        view->has_band = true;
        view->band_start = run_a.to - run_a.from;
        view->band_end = run_b.to - run_b.from;
    }
    // The flattest line inside the band: the places across the road that make the sideways part of the second differences of the points as small as possible
    // (a least squares problem, linear in the places), where a place that would leave the band is held at its edge and the rest solved again. "Sideways" is across
    // the line itself, so the problem is solved again with the sideways direction of the line the last time (the middle curve's to begin with): measured across the
    // middle curve instead the line is free to zigzag along the road, which no sideways part sees. The places are not tied to anything, only (with a little weight,
    // 1e-5) to the middle of the band, to pick among equally flat lines.
    float x[n], z[n];
    float nx[n], nz[n];  // the sideways direction at each sample
    double place[n];
    for (int i = 0; i < n; ++i) {
        place[i] = 0.5;
        x[i] = bx[i] + vx[i] * 0.5f;
        z[i] = bz[i] + vz[i] * 0.5f;
        nx[i] = rx[i];
        nz[i] = rz[i];
    }
    for (int pass = 0; pass < kPasses; ++pass) {
        double matrix[n][n], rhs[n];
        for (int i = 0; i < n; ++i) {
            rhs[i] = 1e-5 * 0.5 * (lo[i] + hi[i]);
            for (int k = 0; k < n; ++k) matrix[i][k] = 0.0;
            matrix[i][i] = 1e-5;
        }
        // One row of the problem: `base` + the sum of `coefficient[m]` * the place of sample `at[m]`, made as small as possible (with a weight).
        const auto add_row = [&](double base, const int *at, const double *coefficient, int count, double weight) {
            for (int m = 0; m < count; ++m) {
                rhs[at[m]] -= weight * base * coefficient[m];
                for (int k = 0; k < count; ++k) matrix[at[m]][at[k]] += weight * coefficient[m] * coefficient[k];
            }
        };
        for (int j = 1; j < kSamples; ++j) {  // the sideways part of the second difference at each sample inside: its curvature
            const double base = (bx[j - 1] - 2.0 * bx[j] + bx[j + 1]) * nx[j] + (bz[j - 1] - 2.0 * bz[j] + bz[j + 1]) * nz[j];
            const int at[3] = {j - 1, j, j + 1};
            const double weight[3] = {1.0, -2.0, 1.0};
            double u[3];
            for (int m = 0; m < 3; ++m) u[m] = weight[m] * (nx[j] * vx[at[m]] + nz[j] * vz[at[m]]);
            add_row(base, at, u, 3, 1.0);
        }
        // The heading at the two ends is the road's (see above): a car arrives at a segment along the road and leaves along it, so the first and the last step of the
        // line have no sideways part along that heading. Without this a segment that turns a corner would be a straight chord (43 degrees in 30 m at a junction came
        // out at a radius of 160 m).
        {
            const int at_start[2] = {0, 1}, at_end[2] = {kSamples - 1, kSamples};
            const double start_coefficient[2] = {-(start_nx * vx[0] + start_nz * vz[0]), start_nx * vx[1] + start_nz * vz[1]};
            const double end_coefficient[2] = {-(end_nx * vx[kSamples - 1] + end_nz * vz[kSamples - 1]), end_nx * vx[kSamples] + end_nz * vz[kSamples]};
            add_row((bx[1] - bx[0]) * start_nx + (bz[1] - bz[0]) * start_nz, at_start, start_coefficient, 2, kHeadingWeight);
            add_row((bx[kSamples] - bx[kSamples - 1]) * end_nx + (bz[kSamples] - bz[kSamples - 1]) * end_nz, at_end, end_coefficient, 2, kHeadingWeight);
        }
        double offset[n];
        for (int i = 0; i < n; ++i) offset[i] = place[i];  // (feasible: the line of the pass before, or the middle of the band)
        solve_box(matrix, rhs, lo, hi, offset);
        for (int i = 0; i < n; ++i) {
            place[i] = std::max(lo[i], std::min(hi[i], offset[i]));
            x[i] = bx[i] + vx[i] * static_cast<float>(place[i]);
            z[i] = bz[i] + vz[i] * static_cast<float>(place[i]);
        }
        for (int i = 1; i < kSamples; ++i) {  // across the line as it is now
            const float tx = x[i + 1] - x[i - 1], tz = z[i + 1] - z[i - 1], l = std::hypot(tx, tz);
            if (l > 1e-4f) {
                nx[i] = tz / l;
                nz[i] = -tx / l;
            }
        }
    }
    int best_at = 0;
    float best_per_sample[kSamples + 1];
    float best = tightest_curvature(x, z, &best_at, best_per_sample);
    if (best > centre) {  // the middle curve is a line through the band as well: the solved line is never allowed to be tighter than it
        best = centre;
        best_at = centre_at;
        if (view) view->used_middle = true;
    } else if (view) {
        for (int i = 0; i <= kSamples; ++i) {
            view->best[i] = {x[i], cy[i], z[i]};
            view->best_curvature[i] = best_per_sample[i];
        }
    }
    const float radius = best < 1e-6f ? kStraight : 1.0f / best;
    return {radius, centre_radius, ts[best_at]};
}

}  // namespace

namespace {

// What corner_limit has worked out, until the network or the settings change. Asked from the picture, the simulation and the thread that works them all out ahead.
struct LimitCache {
    std::vector<CornerLimit> limit;
    std::vector<char> have;
    const void *segments = nullptr;
    unsigned count = 0;
    unsigned epoch = 0;
};
LimitCache g_cache;

bool cache_valid() {  // (with the lock held) makes the cache fit the network; false when there is no network
    const void *segments = *reinterpret_cast<void *const *>(road::kSegments);
    const unsigned count = road::segment_count();
    if (segments != g_cache.segments || count != g_cache.count) {
        g_cache.limit.assign(count, CornerLimit{});
        g_cache.have.assign(count, 0);
        g_cache.segments = segments;
        g_cache.count = count;
        ++g_cache.epoch;
    }
    return count > 0;
}

DWORD WINAPI precompute_all(LPVOID) {
    const void *segments = *reinterpret_cast<void *const *>(road::kSegments);
    for (unsigned i = 0; road::loaded() && i < road::segment_count() && segments == *reinterpret_cast<void *const *>(road::kSegments); ++i) {
        corner_limit(i);
        if ((i & 31) == 31) Sleep(1);
    }
    return 0;
}

}  // namespace

CornerLimit corner_limit(unsigned segment_index) {
    unsigned epoch;
    g_lock.lock();
    if (!cache_valid() || segment_index >= g_cache.count) {
        g_lock.unlock();
        return {kStraight, kStraight, 0.5f};
    }
    if (g_cache.have[segment_index]) {
        const CornerLimit result = g_cache.limit[segment_index];
        g_lock.unlock();
        return result;
    }
    epoch = g_cache.epoch;
    g_lock.unlock();
    const CornerLimit result = work_out_limit(segment_index);  // (not under the lock: it takes a while)
    g_lock.lock();
    if (g_cache.epoch == epoch) {
        g_cache.limit[segment_index] = result;
        g_cache.have[segment_index] = 1;
    }
    g_lock.unlock();
    return result;
}

bool solver_view(unsigned segment_index, SolverView *view) {
    static_assert(SolverView::kCount == kSamples + 1, "one view sample for each sample of the solver");
    SegmentGeometry g;
    if (!segment_geometry(segment_index, &g) || !g.curved) return false;
    work_out_limit(segment_index, view);
    return true;
}

void precompute_limits() {
    static const void *started_for = nullptr;
    const void *segments = *reinterpret_cast<void *const *>(road::kSegments);
    if (!road::loaded() || segments == started_for) return;
    started_for = segments;
    HANDLE thread = CreateThread(nullptr, 0, precompute_all, nullptr, 0, nullptr);
    if (thread) {
        SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
        CloseHandle(thread);
    }
}

}  // namespace improved_ai_opponents::geometry

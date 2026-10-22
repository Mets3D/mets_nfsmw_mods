/*
 * Inspecting the road network (agent_docs/ai-driving.md) from inside the game, seen from wherever the camera is (the free camera of nfsmw_dev_tools
 * included). Nothing here needs the car: the camera's position is found from the view matrix (Lines::eye).
 *
 * The overlay modes Roads and Cost (settings::Overlay; F8 steps through the overlays). Roads:
 *
 * What you see: a small diamond at every road node within kRadius of the camera and a straight line for every road segment between
 * its two nodes, coloured by how fast a car can follow the segment (geometry::corner_limit and the car, car_stats.cpp: red slow to green fast; dark: one that crosses a barrier), and a cross in the middle of the picture: what is "hovered" is
 * what is under it (the free camera takes the mouse for looking, so the middle of the picture is the pointer).
 *   - What is hovered is chosen from a list of candidates: the things that are inside a wide cylinder around the line of sight (nodes: within kPickMetres
 *     of it, and within kHoverFraction of the picture's height of the cross, so that near ones do not catch the whole picture), and of those the one
 *     nearest to the camera. Nodes first: only when there is no node, the segments of the active node are the candidates (nothing is cached).
 *   - The node under the cross is hovered (a white diamond).
 *   - Clicking (left mouse button) a hovered node makes it the active node, the only one there is; clicking it again, or clicking where nothing is
 *     hovered, makes it inactive (but when a segment is hovered as well, the click is for the segment). The active node is an orange diamond and its segments are
 *     orange lines, with its number. What is stored in a node and in a segment is written to the log when it becomes active.
 *   - With a node active, the segments of that node can be hovered too: the hovered one gets its corridor drawn (left and right edge over the drivable
 *     width, curved where the road is, filled in strips) and a label: its number, length, the speed a car can follow it at and the radius of its tightest bend.
 *   - Clicking a hovered segment makes it the active segment (it stays drawn, with its label at its middle, while you look elsewhere), shown as the lanes of
 *     the road's profile coloured by type (segment_draw::add_lanes); clicking it again makes it inactive. A new active node, or none, ends the active segment too.
 * Cost: only the nodes are drawn at first. Pick a node (left click), then the nodes joined to either end of what is picked (yellow; the segment that would be
 * added is previewed while one is hovered): the segments between the picked nodes are drawn, and the route's total time and cost, as the improved AI's route search
 * would cost it for the player's car, for a car that starts the route at 30 m/s and from a stand, are written beside each end of the route. A click on an end node takes it off again, a click on nothing does nothing, the right mouse button starts again.
 * Nothing is read while the road network is not loaded (it is freed when a race ends: road::loaded).
 */
#include "road_inspector.h"

#include <cstdio>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "car_stats.h"
#include "improved_ai_opponents.h"
#include "keys.h"
#include "path_cost.h"
#include "race_area.h"
#include "road_geometry.h"
#include "segment_draw.h"
#include "settings.h"

namespace improved_ai_opponents::road_inspector {

namespace {

using canvas::Clip;
using canvas::Lines;

constexpr DWORD kNode = 0xFF40C0FF, kHover = 0xFFFFFFFF, kActive = 0xFFFFA030, kBarrier = 0xFF605060, kOutside = 0xFF303848;
constexpr DWORD kHoverFill = 0x50FFFFFF;
constexpr DWORD kLabelDim = 0xFFB8C8D8, kPanel = 0xB0282828;
constexpr float kFarSegment = 300.0f;   // a segment longer than this is reported
constexpr float kCrosshairWeight = 2.0f;  // a candidate at the edge of the hover circle counts as this much further away than one in the middle, per its depth
constexpr float kHoverFraction = 0.05f;  // of the picture's height: how far from the cross something may be and still be hovered
constexpr float kRadius = 150.0f;  // nodes and segments farther than this (m) from the camera are not drawn
constexpr float kPickMetres = 4.0f;  // a node is hovered when it is this near the line of sight (metres, at its depth)
constexpr unsigned kNone = 0xFFFFFFFF;

unsigned g_active = kNone, g_active_segment = kNone;  // the active node and the active segment (one of that node's)

struct Hovered {
    unsigned node = kNone, segment = kNone;
};

float distance_between(const float *a, const float *b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// How far (metres) the road's control point at one end is from its node (0 when the segment is straight).
float handle_length(const unsigned char *segment, bool start) {
    const unsigned short length = *reinterpret_cast<const unsigned short *>(segment + (start ? 0xE : 0xC));
    const signed char *h = reinterpret_cast<const signed char *>(segment + (start ? 0x13 : 0x10));
    return length * (500.0f / (127.0f * 65535.0f)) * sqrtf(static_cast<float>(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]));
}

// Long segments are suspicious (a road does not run kilometres between two nodes): said once when the inspector is turned on.
void log_far_segments() {
    int far_count = 0;
    for (unsigned i = 0; i < road::segment_count(); ++i) {
        const unsigned char *s = road::segment(i);
        const float *pa = road::node_position(road::segment_node(s, 0)), *pb = road::node_position(road::segment_node(s, 1));
        if (!pa || !pb) {
            if (far_count++ < 20) g_log.line("road inspector: segment %u has a node (%u, %u) that does not exist", i, road::segment_node(s, 0), road::segment_node(s, 1));
            continue;
        }
        const float length = distance_between(pa, pb);
        if (length > kFarSegment && far_count++ < 20)
            g_log.line("road inspector: segment %u is %.0f m long: node %u (%.0f %.0f %.0f) to node %u (%.0f %.0f %.0f), flags %04X", i, length, road::segment_node(s, 0), pa[0], pa[1], pa[2],
                       road::segment_node(s, 1), pb[0], pb[1], pb[2], road::segment_flags(s));
    }
    g_log.line("road inspector: %d of %u segments are over %.0f m long or have a missing node (the first 20 are listed)", far_count, road::segment_count(), kFarSegment);

    // The game stores each segment's length: the one of its curve. Ours should be the same when the curve is read right: the segments where it is not are listed.
    std::vector<std::pair<float, unsigned>> off;  // (how many percent ours is off, segment)
    for (unsigned i = 0; i < road::segment_count(); ++i) {
        geometry::SegmentGeometry g;
        const unsigned char *s = road::segment(i);
        if (!s || !geometry::segment_geometry(i, &g)) continue;
        const float stored = *reinterpret_cast<const unsigned short *>(s + 4) * (1000.0f / 65535.0f);
        if (stored > 1.0f) off.push_back({100.0f * std::fabs(geometry::curve_length(g) - stored) / stored, i});
    }
    std::sort(off.begin(), off.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    size_t over = 0;
    while (over < off.size() && off[over].first > 3.0f) ++over;
    g_log.line("road inspector: the curve we read is more than 3 %% off the segment's stored length for %zu of %u segments (the worst are listed)", over, road::segment_count());
    for (size_t i = 0; i < std::min<size_t>(over, 15); ++i) {
        geometry::SegmentGeometry g;
        geometry::segment_geometry(off[i].second, &g);
        const unsigned char *s = road::segment(off[i].second);
        g_log.line("road inspector:   segment %u: our curve %.1f m, stored %.1f m (%.0f %% off)", off[i].second, geometry::curve_length(g), *reinterpret_cast<const unsigned short *>(s + 4) * (1000.0f / 65535.0f), off[i].first);
    }
}

std::string limit_text(unsigned segment);

using Text = std::vector<std::pair<std::string, DWORD>>;

struct Rect {
    float x, y, w, h;
};
std::vector<Rect> g_placed;  // the labels of this picture, so that none covers another

float overlap_area(const Rect &a, const Rect &b) {
    const float w = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x), h = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
    return w > 0.0f && h > 0.0f ? w * h : 0.0f;
}

// A label on a blended panel, put beside (x, y) (right, left, below or above it: the first place where it covers no other label, else the one that covers
// the least) and kept inside the picture.
void draw_panel(Lines &lines, const Text &text, float x, float y) {
    const int scale = std::max(2, static_cast<int>(lines.height() / 540.0f));
    const float line_height = Lines::text_height(scale) + 2.0f * scale;
    float panel_width = 0;
    for (const auto &t : text) panel_width = std::max(panel_width, Lines::text_width(t.first.c_str(), scale));
    const float panel_height = line_height * text.size();
    const float margin = 10.0f, w = panel_width + 12.0f, h = panel_height + 6.0f;
    const float candidates[4][2] = {{x + 20.0f, y - panel_height * 0.5f}, {x - 20.0f - panel_width, y - panel_height * 0.5f}, {x - panel_width * 0.5f, y + 26.0f},
                                    {x - panel_width * 0.5f, y - 26.0f - panel_height}};
    Rect best{};
    float best_overlap = 1e30f;
    for (const auto &c : candidates) {
        const Rect r = {std::max(margin, std::min(c[0], lines.width() - panel_width - margin)) - 6.0f, std::max(margin, std::min(c[1], lines.height() - panel_height - margin)) - 4.0f, w, h};
        float overlap = 0.0f;
        for (const Rect &other : g_placed) overlap += overlap_area(r, other);
        if (overlap < best_overlap) {
            best_overlap = overlap;
            best = r;
        }
        if (overlap == 0.0f) break;
    }
    g_placed.push_back(best);
    lines.add_rect(best.x, best.y, best.w, best.h, kPanel);
    float top = best.y + 4.0f;
    for (const auto &t : text) {
        lines.add_text(best.x + 6.0f, top, t.first.c_str(), scale, t.second);
        top += line_height;
    }
}

void draw_node_label(Lines &lines, unsigned index, float screen_x, float screen_y) {
    char line[40];
    snprintf(line, sizeof(line), "NODE %u", index);
    draw_panel(lines, {{line, kActive}}, screen_x, screen_y);
}

// "SEGMENT 12", its length (the stored one, which the route search uses), and its limit speed and radius.
void draw_segment_label(Lines &lines, unsigned index, float x, float y, DWORD color) {
    char line[40], length[40];
    snprintf(line, sizeof(line), "SEGMENT %u%s", index, race_area::contains_segment(index) ? "" : "  OUTSIDE THE RACE");
    const unsigned char *s = road::segment(index);
    snprintf(length, sizeof(length), "LENGTH %.0f M", s ? *reinterpret_cast<const unsigned short *>(s + 4) * (1000.0f / 65535.0f) : 0.0f);
    Text text = {{line, color}, {length, kLabelDim}, {limit_text(index), kLabelDim}};
    geometry::SolverView view;
    if (geometry::solver_view(index, &view)) {
        char band[64];
        if (view.has_band) snprintf(band, sizeof(band), "BAND %.1f M AT THE START, %.1f M AT THE END", view.band_start, view.band_end);
        else snprintf(band, sizeof(band), "NO BAND: THE MIDDLE CURVE");
        text.push_back({band, kLabelDim});
        if (view.used_middle) text.push_back({"THE SOLVED LINE WAS TIGHTER: USING THE MIDDLE", kLabelDim});
    } else {
        text.push_back({"STRAIGHT (NOT FLAGGED CURVED)", kLabelDim});
    }
    draw_panel(lines, text, x, y);
}

std::string flag_names(unsigned flags) {
    static const char *const kNames[16] = {"decision", "no_traffic", "race_forward", "intersection", "entrance", "cops_xor_traffic", "one_way", "shortcut",
                                           "curved", "end_inverted", "start_inverted", "chopper_low", "crosses_barrier", "crosses_player_barrier", "lane_map", "in_race"};
    std::string names;
    for (int bit = 0; bit < 16; ++bit)
        if (flags & (1u << bit)) names += std::string(names.empty() ? "" : "|") + kNames[bit];
    return names.empty() ? "none" : names;
}

std::string lanes_text(unsigned node, bool inverted) {
    road::Lane lanes[15];
    int middle = 0;
    const int count = road::profile_lanes(node, inverted, lanes, &middle);
    char text[96];
    snprintf(text, sizeof(text), "%d lanes, middle lane %d:", count, middle);
    std::string out = text;
    for (int i = 0; i < count; ++i) {
        snprintf(text, sizeof(text), " [%d: type %d, %.1f to %.1f m]", i, lanes[i].type, lanes[i].from, lanes[i].to);
        out += text;
    }
    return out;
}

// What is stored in a node and in its segments, for the log (the screen only shows the ids).
void log_node(unsigned index) {
    const unsigned char *node = road::node(index);
    if (!node) return;
    const float *at = reinterpret_cast<const float *>(node);
    g_log.line("road inspector: node %u at %.1f %.1f %.1f, profile %d, drivable width %.1f m, %d segments", index, at[0], at[1], at[2], *reinterpret_cast<const short *>(node + 0xE),
               road::drivable_width(index), road::node_segment_count(node));
    g_log.line("road inspector:   profile %s", lanes_text(index, false).c_str());
    for (int i = 0; i < std::min(road::node_segment_count(node), 7); ++i) {
        const unsigned segment = road::node_segment(node, i);
        const unsigned char *s = road::segment(segment);
        if (!s) {
            g_log.line("road inspector:   segment %u does not exist", segment);
            continue;
        }
        const unsigned other = road::other_node(s, index);
        const float *to = road::node_position(other);
        g_log.line("road inspector:   segment %u to node %u: %.0f m, road %d, flags %04X (%s), drivable width %.1f m (here) %.1f m (there)", segment, other, to ? distance_between(at, to) : -1.0f,
                   *reinterpret_cast<const short *>(s + 6), road::segment_flags(s), flag_names(road::segment_flags(s)).c_str(), road::drivable_width(index), road::drivable_width(other));
    }
}

void log_segment(unsigned index) {
    const unsigned char *s = road::segment(index);
    if (!s) return;
    const unsigned a = road::segment_node(s, 0), b = road::segment_node(s, 1);
    const float *pa = road::node_position(a), *pb = road::node_position(b);
    const unsigned flags = road::segment_flags(s);
    g_log.line("road inspector: segment %u: node %u to node %u, %.0f m (stored %.0f m), road %d, flags %04X (%s)", index, a, b, pa && pb ? distance_between(pa, pb) : -1.0f,
               *reinterpret_cast<const unsigned short *>(s + 4) * (1000.0f / 65535.0f), *reinterpret_cast<const short *>(s + 6), flags, flag_names(flags).c_str());
    if (!race_area::contains_segment(index)) g_log.line("road inspector:   this segment is outside the race (beyond the track barriers)");
    g_log.line("road inspector:   control points %.1f m from node %u, %.1f m from node %u; %s", handle_length(s, true), a, handle_length(s, false), b, limit_text(index).c_str());
    geometry::SegmentGeometry g;
    if (geometry::segment_geometry(index, &g)) {
        const auto turn = [](const Vec3 &control, float dx, float dz) {  // degrees between the control vector and a direction, in the ground plane
            const float lc = std::hypot(control.x, control.z), ld = std::hypot(dx, dz);
            return lc > 1e-3f && ld > 1e-3f ? std::acos(std::clamp((control.x * dx + control.z * dz) / (lc * ld), -1.0f, 1.0f)) * 57.29578f : 0.0f;
        };
        const float cx = g.node_b.x - g.node_a.x, cz = g.node_b.z - g.node_a.z;
        const geometry::CornerLimit corner = geometry::corner_limit(index);
        Vec3 c[4];
        geometry::side_curve(g, 0.0f, 0.0f, c);
        const Vec3 tight = geometry::bezier_point(c, corner.tightest_t);
        g_log.line("road inspector:   our curve %.1f m (stored %.1f m), %s; its start leaves %.0f deg and its end arrives %.0f deg off the straight line between the nodes; the best line is tightest %.0f %% of the way along, at %.0f %.0f %.0f",
                   geometry::curve_length(g), *reinterpret_cast<const unsigned short *>(s + 4) * (1000.0f / 65535.0f), g.curved ? "curved" : "straight", turn(g.start_control, cx, cz), turn(g.end_control, -cx, -cz),
                   100.0f * corner.tightest_t, tight.x, tight.y, tight.z);
    }
    g_log.line("road inspector:   at node %u: %s", a, lanes_text(a, (flags & road::kFlagStartInverted) != 0).c_str());
    g_log.line("road inspector:   at node %u: %s", b, lanes_text(b, (flags & road::kFlagEndInverted) != 0).c_str());
}

// The distance in pixels from a point to a line in pixels, and how far along the line the nearest point is (0 to 1).
float pixel_distance(float px, float py, const float l[4], float *along) {
    const float dx = l[2] - l[0], dy = l[3] - l[1];
    const float length2 = dx * dx + dy * dy;
    float t = length2 > 0.0f ? ((px - l[0]) * dx + (py - l[1]) * dy) / length2 : 0.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    *along = t;
    return hypotf(px - (l[0] + t * dx), py - (l[1] + t * dy));
}

DWORD speed_color(float speed);

// What the best-line solver worked with, drawn over the segment: the Bezier control points of the middle curve with their handles (amber), the middle curve (white),
// the 25 samples with the band across the road at each (grey ticks) and its two edges (cyan), and the best line (coloured by the speed its curvature allows, red slow
// to green fast: the limit speed is that of its slowest stretch). Drawn at three heights (handles and band low, the middle curve and the best line above) so that they
// do not hide each other.
void add_solver_view(Lines &lines, unsigned segment) {
    geometry::SolverView view;
    if (!geometry::solver_view(segment, &view)) return;
    constexpr DWORD kHandle = 0xFFFFC000, kTick = 0xFF708090, kBand = 0xFF40C0FF, kMiddle = 0xFFFFFFFF;
    const auto up = [](Vec3 p, float height) {
        p.y += height;
        return p;
    };
    lines.add(up(view.control[0], 0.4f), up(view.control[1], 0.4f), kHandle);
    lines.add(up(view.control[3], 0.4f), up(view.control[2], 0.4f), kHandle);
    canvas::add_diamond(lines, up(view.control[1], 0.4f), 0.5f, kHandle);
    canvas::add_diamond(lines, up(view.control[2], 0.4f), 0.5f, kHandle);
    const car_stats::Stats &car = car_stats::of(-1);
    for (int i = 0; i < geometry::SolverView::kCount; ++i) {
        if (view.has_band) lines.add(up(view.left[i], 0.4f), up(view.right[i], 0.4f), kTick);
        if (i == 0) continue;
        if (view.has_band) {
            lines.add(up(view.left[i - 1], 0.4f), up(view.left[i], 0.4f), kBand);
            lines.add(up(view.right[i - 1], 0.4f), up(view.right[i], 0.4f), kBand);
        }
        lines.add(up(view.centre[i - 1], 0.6f), up(view.centre[i], 0.6f), kMiddle);
        const float curvature = 0.5f * (view.best_curvature[i - 1] + view.best_curvature[i]);
        lines.add(up(view.best[i - 1], 0.9f), up(view.best[i], 0.9f), speed_color(car_stats::cornering_speed(car, curvature < 1e-6f ? 1e9f : 1.0f / curvature)));
    }
}

// A diamond where the best line through a segment is at its tightest (the bend that sets its limit speed).
void add_tightest_marker(Lines &lines, unsigned segment) {
    geometry::SegmentGeometry g;
    if (!geometry::segment_geometry(segment, &g)) return;
    Vec3 c[4];
    geometry::side_curve(g, 0.0f, 0.0f, c);
    Vec3 p = geometry::bezier_point(c, geometry::corner_limit(segment).tightest_t);
    p.y += 0.3f;
    canvas::add_diamond(lines, p, 1.2f, 0xFFFF00FF);
    canvas::add_pole(lines, p, 4.0f, 0xFFFF00FF);
}

// How fast a car can follow a segment, as a colour: red at 8 m/s and below, yellow at about 25, green from 45 m/s on.
DWORD speed_color(float speed) {
    const float t = std::max(0.0f, std::min(1.0f, (speed - 8.0f) / 37.0f));
    const int r = static_cast<int>(255.0f * (t < 0.5f ? 1.0f : 2.0f * (1.0f - t))), g = static_cast<int>(255.0f * (t < 0.5f ? 2.0f * t : 1.0f));
    return 0xFF000000 | (r << 16) | (g << 8) | 0x30;
}

// The limit of a segment: the speed a car can follow it at (the first player's car, else the ini's numbers), the radius of its best line and of its middle.
struct Limit {
    float speed, radius, centre_radius;
};
Limit limit_of(unsigned segment) {
    const geometry::CornerLimit corner = geometry::corner_limit(segment);
    return {car_stats::cornering_speed(car_stats::of(-1), corner.radius), corner.radius, corner.centre_radius};
}

DWORD segment_color(unsigned index, unsigned flags) {
    if (!race_area::contains_segment(index)) return kOutside;
    return (flags & road::kFlagCrossesBarrier) ? kBarrier : speed_color(limit_of(index).speed);
}

// "limit 23 m/s, radius 14 m" (the radius of the tightest stretch of the best line through the road; the middle curve's when that is a lot tighter)
std::string limit_text(unsigned segment) {
    const Limit limit = limit_of(segment);
    char text[64];
    if (limit.radius >= 1e8f) snprintf(text, sizeof(text), "limit %.0f m/s, straight", limit.speed);
    else snprintf(text, sizeof(text), "limit %.0f m/s, radius %.0f m", limit.speed, limit.radius);
    if (limit.centre_radius < 0.8f * limit.radius) {  // the best line through the road is flatter than the middle of it
        char more[48];
        snprintf(more, sizeof(more), " (middle %.0f m)", limit.centre_radius);
        return std::string(text) + more;
    }
    return text;
}


constexpr DWORD kCandidate = 0xFFFFE000, kRouteFill = 0x40FFA030;

// The camera and the picture, as the picking of nodes needs them.
struct View {
    Lines &lines;
    float eye_at[3];
    float centre_x, centre_y, focal, hover_pixels;
};

struct NodeLook {
    float size;
    DWORD color;
};

// Draws the nodes near the camera as `look(node)` says, and returns the hovered one: of the `allowed` nodes under the cross, the nearest to the camera (kNone if none).
template <class Look, class Allowed> unsigned draw_nodes(const View &v, Look look, Allowed allowed) {
    unsigned hovered = kNone;
    float nearest = 1e30f;  // the rank (depth, more for what is further from the cross) of the hovered node
    for (unsigned i = 0; i < road::node_count(); ++i) {
        const float *at = road::node_position(i);
        if (!at) break;
        if (distance_between(at, v.eye_at) > kRadius) continue;
        const Vec3 p = {at[0], at[1] + 0.3f, at[2]};
        const Clip c = v.lines.project(p);
        if (c.w < 0.5f) continue;  // behind the camera
        const NodeLook style = look(i);
        canvas::add_diamond(v.lines, p, style.size, style.color);
        if (!allowed(i)) continue;
        float sx, sy;
        v.lines.to_pixels(c, &sx, &sy);
        const float off_x = sx - v.centre_x, off_y = sy - v.centre_y;
        const float from_cross = sqrtf(off_x * off_x + off_y * off_y);
        const float lateral = from_cross / v.focal * c.w;  // metres off the line of sight, at the node's depth
        if (lateral > kPickMetres || from_cross > v.hover_pixels) continue;
        const float rank = c.w * (1.0f + kCrosshairWeight * from_cross / v.hover_pixels);
        if (rank < nearest) {
            nearest = rank;
            hovered = i;
        }
    }
    return hovered;
}

void draw_cross(Lines &lines) {
    const float centre_x = lines.width() * 0.5f, centre_y = lines.height() * 0.5f;
    lines.add_screen(centre_x - 8.0f, centre_y, centre_x + 8.0f, centre_y, kHover);
    lines.add_screen(centre_x, centre_y - 8.0f, centre_x, centre_y + 8.0f, kHover);
}

// The segment that joins two nodes, or kNone.
unsigned segment_between(unsigned a, unsigned b) {
    const unsigned char *node = road::node(a);
    for (int i = 0; node && i < std::min(road::node_segment_count(node), 7); ++i) {
        const unsigned segment = road::node_segment(node, i);
        const unsigned char *s = road::segment(segment);
        if (s && road::other_node(s, a) == b) return segment;
    }
    return kNone;
}

// ---- Roads: the road network; the segments of the active node.
void add_roads(const View &v, int player) {
    Lines &lines = v.lines;
    const float *eye_at = v.eye_at;
    const float centre_x = v.centre_x, centre_y = v.centre_y, hover_pixels = v.hover_pixels;
    if (g_active != kNone && !road::node(g_active)) g_active = kNone;
    if (g_active == kNone || !road::segment(g_active_segment)) g_active_segment = kNone;

    // The segments near the camera, as straight lines.
    for (unsigned i = 0; i < road::segment_count(); ++i) {
        const unsigned char *s = road::segment(i);
        const float *pa = road::node_position(road::segment_node(s, 0)), *pb = road::node_position(road::segment_node(s, 1));
        if (!pa || !pb) continue;
        if (distance_between(pa, eye_at) > kRadius && distance_between(pb, eye_at) > kRadius) continue;
        lines.add({pa[0], pa[1] + 0.3f, pa[2]}, {pb[0], pb[1] + 0.3f, pb[2]}, segment_color(i, road::segment_flags(s)));
    }

    // The nodes near the camera, and the one hovered.
    Hovered hovered;
    hovered.node = draw_nodes(v, [](unsigned i) { return NodeLook{0.6f, race_area::contains_node(i) ? kNode : kOutside}; }, [](unsigned) { return true; });

    // With a node active: its segments are orange, and the one nearest to the cross (when no other node is hovered) is hovered.
    if (g_active != kNone) {
        const unsigned char *node = road::node(g_active);
        const float *at = reinterpret_cast<const float *>(node);
        float nearest_segment = 1e30f;
        for (int i = 0; i < std::min(road::node_segment_count(node), 7); ++i) {
            const unsigned index = road::node_segment(node, i);
            const unsigned char *s = road::segment(index);
            if (!s) continue;
            const float *pa = road::node_position(road::segment_node(s, 0)), *pb = road::node_position(road::segment_node(s, 1));
            if (!pa || !pb) continue;
            const Vec3 a = {pa[0], pa[1] + 0.3f, pa[2]}, b = {pb[0], pb[1] + 0.3f, pb[2]};
            if (index != g_active_segment) lines.add(a, b, kActive);
            float pixels[4];
            if ((hovered.node == kNone || hovered.node == g_active) && lines.to_pixel_line(a, b, pixels)) {
                float along;
                const float from_cross = pixel_distance(centre_x, centre_y, pixels, &along);
                if (from_cross <= hover_pixels) {
                    const Clip ca = lines.project(a), cb = lines.project(b);
                    const float depth = ca.w + (cb.w - ca.w) * along;  // at the nearest point (the pixels' parameter, close enough)
                    const float rank = depth * (1.0f + kCrosshairWeight * from_cross / hover_pixels);
                    if (rank < nearest_segment) {
                        nearest_segment = rank;
                        hovered.segment = index;
                    }
                }
            }
        }
        canvas::add_diamond(lines, {at[0], at[1] + 0.3f, at[2]}, 1.5f, kActive);
        const Clip c = lines.project({at[0], at[1] + 0.3f, at[2]});
        if (c.w >= 0.5f) {
            float sx, sy;
            lines.to_pixels(c, &sx, &sy);
            draw_node_label(lines, g_active, sx, sy);
        }
    }
    if (hovered.node != kNone && hovered.node != g_active) {
        const float *at = road::node_position(hovered.node);
        canvas::add_diamond(lines, {at[0], at[1] + 0.3f, at[2]}, 1.2f, kHover);
    }
    if (g_active_segment != kNone) {
        segment_draw::add_lanes(lines, g_active_segment);
        segment_draw::add_corridor(lines, g_active_segment, kActive, 0);
        add_tightest_marker(lines, g_active_segment);
        add_solver_view(lines, g_active_segment);
        const unsigned char *s = road::segment(g_active_segment);
        const float *pa = road::node_position(road::segment_node(s, 0)), *pb = road::node_position(road::segment_node(s, 1));
        if (pa && pb) {
            const Clip c = lines.project({(pa[0] + pb[0]) * 0.5f, (pa[1] + pb[1]) * 0.5f + 0.3f, (pa[2] + pb[2]) * 0.5f});
            float sx, sy;
            if (c.w >= 0.5f) {
                lines.to_pixels(c, &sx, &sy);
                draw_segment_label(lines, g_active_segment, sx, sy, kActive);
            }
        }
    }
    if (hovered.segment != kNone && hovered.segment != g_active_segment) {
        segment_draw::add_corridor(lines, hovered.segment, kHover, kHoverFill);
        add_tightest_marker(lines, hovered.segment);
        add_solver_view(lines, hovered.segment);
        draw_segment_label(lines, hovered.segment, centre_x, centre_y, kHover);
    }

    // The cross in the middle of the picture.
    lines.add_screen(centre_x - 8.0f, centre_y, centre_x + 8.0f, centre_y, kHover);
    lines.add_screen(centre_x, centre_y - 8.0f, centre_x, centre_y + 8.0f, kHover);

    // A click (once per press: the pictures of all players see the same state)
    // (only the first player's picture handles clicks: the same click would otherwise be handled again in the second player's picture, with what is under the cross
    // there)
    static bool before = false;
    if (player == 0 && nfsmw::key_clicked(VK_LBUTTON, before)) {
        const unsigned previous_node = g_active, previous_segment = g_active_segment;
        if (hovered.node != kNone && hovered.node != g_active) {
            g_active = hovered.node;
            g_active_segment = kNone;
        } else if (hovered.segment != kNone) {  // (also when the cross is on the active node as well: the segment that is highlighted wins)
            g_active_segment = hovered.segment == g_active_segment ? kNone : hovered.segment;
        } else {  // the active node again, or nothing: nothing is active
            g_active = g_active_segment = kNone;
        }
        g_log.line("road inspector: active node %d, active segment %d", g_active == kNone ? -1 : static_cast<int>(g_active), g_active_segment == kNone ? -1 : static_cast<int>(g_active_segment));
        if (g_active != previous_node && g_active != kNone) log_node(g_active);
        if (g_active_segment != previous_segment && g_active_segment != kNone) log_segment(g_active_segment);
    }
}

// ---- Cost: a route picked node by node, with what it costs.
std::vector<unsigned> g_route;  // the nodes of the route in order, each joined to the next by a segment

// The route's totals (for a car that starts the route at 30 m/s and for one that starts at a stand), in a label at each end of the route that is in view: where
// you are looking at the route, and not at a fixed place in the picture (which the minimap and the other HUD would cover). With neither end in view, at the top.
void draw_route_labels(const View &v, const std::vector<unsigned> &segments, int player) {
    Lines &lines = v.lines;
    Text summary;
    char line[96];
    for (const float start : {30.0f, 0.0f}) {
        float speed = start, length = 0.0f, seconds = 0.0f, cost = 0.0f;
        for (const unsigned segment : segments) {
            const path_cost::StepCost step = path_cost::cost_of_step(segment, speed, player);
            speed = step.exit;
            length += step.length;
            seconds += step.seconds;
            cost += step.cost;
        }
        if (start > 1.0f) {
            snprintf(line, sizeof(line), "ROUTE: %zu SEGMENTS, %.0f M", segments.size(), length);
            summary.push_back({line, kActive});
        }
        snprintf(line, sizeof(line), "FROM %.0f M/S: %.1f S, COST %.0f M (+%.0f)", start, seconds, cost, cost - length);
        summary.push_back({line, kLabelDim});
    }
    bool drawn = false;
    for (const unsigned node : {g_route.front(), g_route.back()}) {
        const float *at = road::node_position(node);
        if (!at) continue;
        const Clip c = lines.project({at[0], at[1] + 0.3f, at[2]});
        if (c.w < 0.5f) continue;
        float sx, sy;
        lines.to_pixels(c, &sx, &sy);
        draw_panel(lines, summary, sx, sy);
        drawn = true;
    }
    if (!drawn) draw_panel(lines, summary, v.centre_x, 70.0f);
}

void add_cost(const View &v, int player) {
    Lines &lines = v.lines;
    for (const unsigned n : g_route)
        if (!road::node(n)) {
            g_route.clear();
            break;
        }
    const bool empty = g_route.empty();
    const unsigned front = empty ? kNone : g_route.front(), back = empty ? kNone : g_route.back();
    const auto in_route = [&](unsigned n) { return std::find(g_route.begin(), g_route.end(), n) != g_route.end(); };
    const auto is_end = [&](unsigned n) { return !empty && (n == front || n == back); };
    const auto is_candidate = [&](unsigned n) { return !empty && !in_route(n) && (segment_between(back, n) != kNone || segment_between(front, n) != kNone); };

    // Only the nodes that can be picked are hovered: any node to begin with, then the nodes joined to either end of the route, and the two ends (to take them off again).
    const unsigned hovered = draw_nodes(
        v,
        [&](unsigned n) {
            if (in_route(n)) return NodeLook{1.2f, kActive};
            if (is_candidate(n)) return NodeLook{0.9f, kCandidate};
            return NodeLook{0.5f, empty && race_area::contains_node(n) ? kNode : kOutside};
        },
        [&](unsigned n) { return empty || is_end(n) || is_candidate(n); });

    std::vector<unsigned> segments;
    for (size_t i = 0; i + 1 < g_route.size(); ++i) {
        const unsigned segment = segment_between(g_route[i], g_route[i + 1]);
        if (segment != kNone) segments.push_back(segment);
    }
    for (const unsigned segment : segments) segment_draw::add_corridor(lines, segment, kActive, kRouteFill);
    if (hovered != kNone) {
        const float *at = road::node_position(hovered);
        canvas::add_diamond(lines, {at[0], at[1] + 0.3f, at[2]}, 1.4f, kHover);
        if (is_candidate(hovered)) {  // the segment it would add
            const unsigned toward_back = segment_between(back, hovered);
            segment_draw::add_corridor(lines, toward_back != kNone ? toward_back : segment_between(front, hovered), kHover, kHoverFill);
        }
    }
    if (!segments.empty()) draw_route_labels(v, segments, player);
    draw_cross(lines);

    // Left click: pick the hovered node (at the end of the route it is joined to), or take it off when it is an end of the route. Right click: start again.
    static bool left_before = false, right_before = false;
    // (only the first player's picture handles clicks, see add_roads)
    if (player == 0 && nfsmw::key_clicked(VK_LBUTTON, left_before) && hovered != kNone) {
        if (empty) g_route.push_back(hovered);
        else if (is_end(hovered)) {
            if (hovered == back) g_route.pop_back();
            else g_route.erase(g_route.begin());
        } else if (segment_between(back, hovered) != kNone) g_route.push_back(hovered);
        else g_route.insert(g_route.begin(), hovered);
    }
    if (player == 0 && nfsmw::key_clicked(VK_RBUTTON, right_before)) g_route.clear();
}

}  // namespace

void set_mode(int player, int mode) {
    static int last[2] = {settings::kOverlayOff, settings::kOverlayOff};
    int &was = last[player != 0 ? 1 : 0];
    if (was == mode) return;
    was = mode;
    g_active = g_active_segment = kNone;  // a selection belongs to a mode
    g_route.clear();
    if ((mode == settings::kOverlayRoads || mode == settings::kOverlayCost) && road::loaded()) {
        g_log.line("road inspector: %s mode: %u nodes, %u segments, %u profiles", mode == settings::kOverlayRoads ? "roads" : "cost", road::node_count(), road::segment_count(),
                   *reinterpret_cast<const unsigned *>(road::kNumProfiles));
        log_far_segments();
    }
}

void begin_picture() { g_placed.clear(); }

void add(Lines &lines, int player, int mode) {
    Vec3 eye;
    if (!road::loaded()) {  // the race is over: the next track has other nodes
        g_active = g_active_segment = kNone;
        g_route.clear();
        return;
    }
    if (!lines.eye(&eye)) return;
    const View v = {lines, {eye.x, eye.y, eye.z}, lines.width() * 0.5f, lines.height() * 0.5f, 0.5f * lines.height() * lines.matrix()[5], kHoverFraction * lines.height()};
    if (mode == settings::kOverlayCost) add_cost(v, player);
    else add_roads(v, player);
}

}  // namespace improved_ai_opponents::road_inspector

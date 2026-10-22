/*
 * Inspecting the road network (agent_docs/ai-driving.md) from inside the game, seen from wherever the camera is (the free camera of nfsmw_dev_tools
 * included). Nothing here needs the car: the camera's position is found from the view matrix (Lines::eye).
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
 * Nothing is read while the road network is not loaded (it is freed when a race ends: road::loaded).
 */
#include "road_inspector.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "car_stats.h"
#include "improved_ai_opponents.h"
#include "keys.h"
#include "race_area.h"
#include "road_geometry.h"
#include "segment_draw.h"

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

bool g_on = false;
int g_key = VK_F11;
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
    draw_panel(lines, {{line, color}, {length, kLabelDim}, {limit_text(index), kLabelDim}}, x, y);
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

}  // namespace

void install() {
    char key[16];
    GetPrivateProfileStringA("road_inspector", "toggle_key", "F11", key, sizeof(key), g_ini);
    g_key = nfsmw::key_from_name(key);
}

bool enabled() {
    static bool before = false;
    if (nfsmw::key_pressed(nfsmw::game_has_focus() && nfsmw::key_down(g_key), before)) {
        g_on = !g_on;
        g_log.line("road inspector: %s", g_on ? "on" : "off");
        if (g_on && road::loaded()) g_log.line("road inspector: %u nodes, %u segments, %u profiles", road::node_count(), road::segment_count(), *reinterpret_cast<const unsigned *>(road::kNumProfiles));
        if (g_on) log_far_segments();
        g_active = g_active_segment = kNone;
    }
    static bool was_loaded = false;
    const bool now_loaded = road::loaded();
    if (was_loaded && !now_loaded) g_active = g_active_segment = kNone;  // the race is over: the next track has other nodes
    was_loaded = now_loaded;
    return g_on;
}

void begin_picture() { g_placed.clear(); }

void add(Lines &lines) {
    Vec3 eye;
    if (!road::loaded() || !lines.eye(&eye)) return;
    const float eye_at[3] = {eye.x, eye.y, eye.z};
    const float centre_x = lines.width() * 0.5f, centre_y = lines.height() * 0.5f;
    const float focal = 0.5f * lines.height() * lines.matrix()[5];  // pixels per unit of tan(angle)
    const float hover_pixels = kHoverFraction * lines.height();
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
    float nearest = 1e30f;  // the rank (depth, more for what is further from the cross) of the hovered node
    for (unsigned i = 0; i < road::node_count(); ++i) {
        const float *at = road::node_position(i);
        if (!at) break;
        const float distance = distance_between(at, eye_at);
        if (distance > kRadius) continue;
        const Vec3 p = {at[0], at[1] + 0.3f, at[2]};
        const Clip c = lines.project(p);
        if (c.w < 0.5f) continue;  // behind the camera
        canvas::add_diamond(lines, p, 0.6f, race_area::contains_node(i) ? kNode : kOutside);
        float sx, sy;
        lines.to_pixels(c, &sx, &sy);
        const float off_x = sx - centre_x, off_y = sy - centre_y;
        const float from_cross = sqrtf(off_x * off_x + off_y * off_y);
        const float lateral = from_cross / focal * c.w;  // metres off the line of sight, at the node's depth
        if (lateral > kPickMetres || from_cross > hover_pixels) continue;
        const float rank = c.w * (1.0f + kCrosshairWeight * from_cross / hover_pixels);
        if (rank < nearest) {
            nearest = rank;
            hovered.node = i;
        }
    }

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
        draw_segment_label(lines, hovered.segment, centre_x, centre_y, kHover);
    }

    // The cross in the middle of the picture.
    lines.add_screen(centre_x - 8.0f, centre_y, centre_x + 8.0f, centre_y, kHover);
    lines.add_screen(centre_x, centre_y - 8.0f, centre_x, centre_y + 8.0f, kHover);

    // A click (once per press: the pictures of all players see the same state)
    static bool before = false;
    if (nfsmw::key_pressed(nfsmw::game_has_focus() && nfsmw::key_down(VK_LBUTTON), before)) {
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

}  // namespace improved_ai_opponents::road_inspector

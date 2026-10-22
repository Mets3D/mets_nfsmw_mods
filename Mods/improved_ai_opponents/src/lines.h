// Lines drawn into a player's picture: points in the world (the simulation's axes) projected with the view's WorldClipMatrix, and 2D lines and text in
// pixels. Collected in a batch and drawn together (draw_lines). Shared by the AI overlay (overlay.cpp) and the road inspector (road_inspector.cpp).
#pragma once

#include <d3d9.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "vec3.h"

namespace improved_ai_opponents::canvas {

struct Vertex {
    float x, y, z, rhw;
    DWORD color;
};
struct Clip {
    float x, y, z, w;
};

class Lines {
  public:
    Lines(const float *world_clip, float width, float height) : m_(world_clip), width_(width), height_(height) {}

    void add(const Vec3 &a, const Vec3 &b, DWORD color) {
        Clip p = project(a), q = project(b);
        if (clip_near(p, q)) push(screen(p, color), screen(q, color), vertices_);
    }
    // A line in pixels of the picture (no depth: drawn over everything, also over the rectangles).
    void add_screen(float x0, float y0, float x1, float y1, DWORD color) { push({x0, y0, 0.0f, 1.0f, color}, {x1, y1, 0.0f, 1.0f, color}, top_); }
    // A filled triangle in the world (the colour's alpha counts), drawn first, under the lines, with the lines' depth test.
    void add_triangle(const Vec3 &a, const Vec3 &b, const Vec3 &c, DWORD color) {
        Clip in[3] = {project(a), project(b), project(c)}, out[4];
        int count = 0;
        for (int i = 0; i < 3; ++i) {  // cut at the near plane (w = kNear)
            const Clip &p = in[i], &q = in[(i + 1) % 3];
            const bool p_in = p.w >= kNear, q_in = q.w >= kNear;
            if (p_in) out[count++] = p;
            if (p_in != q_in) out[count++] = lerp(p, q, (kNear - p.w) / (q.w - p.w));
        }
        for (int i = 1; i + 1 < count; ++i) {
            fills_.push_back(screen(out[0], color));
            fills_.push_back(screen(out[i], color));
            fills_.push_back(screen(out[i + 1], color));
        }
    }
    void add_quad(const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, DWORD color) {  // a, b, c, d in order around it
        add_triangle(a, b, c, color);
        add_triangle(a, c, d, color);
    }
    // A filled rectangle in pixels (the colour's alpha counts), under the 2D lines and over the 3D ones.
    void add_rect(float x, float y, float w, float h, DWORD color) {
        const Vertex a = {x, y, 0.0f, 1.0f, color}, b = {x + w, y, 0.0f, 1.0f, color}, c = {x + w, y + h, 0.0f, 1.0f, color}, d = {x, y + h, 0.0f, 1.0f, color};
        for (const Vertex &v : {a, b, c, a, c, d}) rects_.push_back(v);
    }
    // Text in pixels (upper case, digits and a few signs: see lines.cpp), `scale` pixels per dot of the font, with a black shadow. Returns the width.
    float add_text(float x, float y, const char *text, int scale, DWORD color);
    static float text_width(const char *text, int scale);
    static float text_height(int scale) { return 7.0f * scale; }

    Clip project(const Vec3 &sim) const {  // [x y z 1] * M, as D3DXVec4Transform does
        const Vec3 p = {sim.z, -sim.x, sim.y};  // the simulation's axes (y up) to the renderer's (z up): bConvertFromBond
        return {p.x * m_[0] + p.y * m_[4] + p.z * m_[8] + m_[12], p.x * m_[1] + p.y * m_[5] + p.z * m_[9] + m_[13],
                p.x * m_[2] + p.y * m_[6] + p.z * m_[10] + m_[14], p.x * m_[3] + p.y * m_[7] + p.z * m_[11] + m_[15]};
    }
    // Pixels of the picture from a clip position in front of the camera (w > 0).
    void to_pixels(const Clip &c, float *x, float *y) const {
        *x = (c.x / c.w + 1.0f) * width_ * 0.5f;
        *y = (1.0f - c.y / c.w) * height_ * 0.5f;
    }
    // A world line as pixels of the picture, {x0, y0, x1, y1}, cut at the camera's near plane; false when all of it is behind the camera.
    bool to_pixel_line(const Vec3 &a, const Vec3 &b, float out[4]) const {
        Clip p = project(a), q = project(b);
        if (!clip_near(p, q)) return false;
        to_pixels(p, &out[0], &out[1]);
        to_pixels(q, &out[2], &out[3]);
        return true;
    }
    // The camera's position (simulation axes): the point whose clip x, y and w are all 0. False if the matrix has none.
    bool eye(Vec3 *out) const;
    const float *matrix() const { return m_; }
    float width() const { return width_; }
    float height() const { return height_; }
    bool empty() const { return vertices_.empty() && rects_.empty() && top_.empty() && fills_.empty(); }
    const std::vector<Vertex> &fills() const { return fills_; }       // triangles in the world
    const std::vector<Vertex> &vertices() const { return vertices_; }  // the lines in the world
    const std::vector<Vertex> &rects() const { return rects_; }       // triangles
    const std::vector<Vertex> &top_vertices() const { return top_; }  // the lines in pixels

  private:
    static constexpr float kNear = 0.1f;  // the camera's near plane (clip w)
    // Cuts a line at the near plane; false when all of it is behind the camera.
    static bool clip_near(Clip &p, Clip &q) {
        if (p.w < kNear && q.w < kNear) return false;
        if (p.w < kNear) p = lerp(p, q, (kNear - p.w) / (q.w - p.w));
        else if (q.w < kNear) q = lerp(q, p, (kNear - q.w) / (p.w - q.w));
        return true;
    }
    static Clip lerp(const Clip &from, const Clip &to, float t) {
        return {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t, from.w + (to.w - from.w) * t};
    }
    Vertex screen(const Clip &c, DWORD color) const {
        const float inv = 1.0f / c.w;
        // z for the depth test: the clip depth, a little nearer so that a line on the road is not hidden by the road itself.
        const float z = std::fmin(1.0f, std::fmax(0.0f, c.z * inv - 0.0004f));
        return {(c.x * inv + 1.0f) * width_ * 0.5f, (1.0f - c.y * inv) * height_ * 0.5f, z, 1.0f, color};
    }
    static void push(const Vertex &v0, const Vertex &v1, std::vector<Vertex> &into) {
        // A few pixels thick: the same line a pixel over.
        for (int i = 0; i < 4; ++i) {
            const float dx = static_cast<float>(i & 1), dy = static_cast<float>(i >> 1);
            Vertex a2 = v0, b2 = v1;
            a2.x += dx;
            b2.x += dx;
            a2.y += dy;
            b2.y += dy;
            into.push_back(a2);
            into.push_back(b2);
        }
    }
    const float *m_;
    float width_, height_;
    std::vector<Vertex> vertices_, rects_, top_, fills_;
};

// Shapes in the world: a pole, a cross and a diamond, square and X in the ground plane.
void add_pole(Lines &lines, const Vec3 &p, float height, DWORD color);
void add_cross(Lines &lines, const Vec3 &p, float r, DWORD color);
void add_diamond(Lines &lines, const Vec3 &p, float r, DWORD color);
void add_square(Lines &lines, const Vec3 &p, float r, DWORD color);

// Draws the batch into the device's current target: the filled triangles in the world (blended), the lines in the world (the game's depth buffer hides what is behind a wall), then the rectangles
// (blended), then the lines in pixels.
void draw_lines(IDirect3DDevice9 *device, const Lines &lines);

}  // namespace improved_ai_opponents::canvas

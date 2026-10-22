#include "lines.h"

#include <cctype>
#include <cstring>

namespace improved_ai_opponents::canvas {

namespace {

// A 5 x 7 font: seven rows of five bits (bit 4 is the left dot) for the characters the labels use. Lower case is drawn as upper case.
struct Glyph {
    char c;
    unsigned char rows[7];
};
constexpr Glyph kFont[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}}, {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}}, {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}}, {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}}, {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}}, {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}}, {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}}, {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}}, {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}}, {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}}, {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}}, {'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}}, {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}}, {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}}, {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}}, {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}}, {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}}, {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}}, {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}}, {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
};
constexpr int kAdvance = 6;  // dots per character: five and a gap

const Glyph *glyph_of(char c) {
    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (c == ' ') return nullptr;
    for (const Glyph &g : kFont)
        if (g.c == c) return &g;
    return &kFont[sizeof(kFont) / sizeof(kFont[0]) - 1];  // '?'
}

}  // namespace

float Lines::text_width(const char *text, int scale) { return static_cast<float>(strlen(text) * kAdvance * scale - scale); }

float Lines::add_text(float x, float y, const char *text, int scale, DWORD color) {
    const float s = static_cast<float>(scale);
    for (int pass = 0; pass < 2; ++pass) {  // the shadow first, then the text over it
        const float off = pass == 0 ? 1.0f : 0.0f;
        const DWORD c = pass == 0 ? 0xFF000000 : color;
        float cx = x;
        for (const char *p = text; *p; ++p, cx += kAdvance * s) {
            const Glyph *g = glyph_of(*p);
            if (!g) continue;
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5;) {
                    if (!((g->rows[row] >> (4 - col)) & 1)) {
                        ++col;
                        continue;
                    }
                    int end = col;  // a run of dots in the row is one line
                    while (end + 1 < 5 && ((g->rows[row] >> (4 - (end + 1))) & 1)) ++end;
                    const float py = y + row * s + 0.5f + off;
                    add_screen(cx + col * s + 0.5f + off, py, cx + (end + 1) * s - 0.5f + off, py, c);
                    col = end + 1;
                }
            }
        }
    }
    return text_width(text, scale);
}

bool Lines::eye(Vec3 *out) const {
    // clip x, y and w are linear in the position p (the renderer's axes): A p = -b with column c of the matrix for c = 0, 1, 3.
    const int cols[3] = {0, 1, 3};
    double a[3][3], b[3];
    for (int r = 0; r < 3; ++r) {
        for (int i = 0; i < 3; ++i) a[r][i] = m_[i * 4 + cols[r]];
        b[r] = -m_[12 + cols[r]];
    }
    const auto det = [](const double m[3][3]) {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    };
    const double d = det(a);
    if (std::fabs(d) < 1e-12) return false;
    double p[3];
    for (int i = 0; i < 3; ++i) {
        double t[3][3];
        memcpy(t, a, sizeof(t));
        for (int r = 0; r < 3; ++r) t[r][i] = b[r];
        p[i] = det(t) / d;
    }
    *out = {static_cast<float>(-p[1]), static_cast<float>(p[2]), static_cast<float>(p[0])};  // back to the simulation's axes
    return true;
}

void add_pole(Lines &lines, const Vec3 &p, float height, DWORD color) { lines.add(p, {p.x, p.y + height, p.z}, color); }

void add_cross(Lines &lines, const Vec3 &p, float r, DWORD color) {
    lines.add({p.x - r, p.y, p.z}, {p.x + r, p.y, p.z}, color);
    lines.add({p.x, p.y, p.z - r}, {p.x, p.y, p.z + r}, color);
}

namespace {
void add_loop(Lines &lines, const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, DWORD color) {
    lines.add(a, b, color);
    lines.add(b, c, color);
    lines.add(c, d, color);
    lines.add(d, a, color);
}
}  // namespace

void add_diamond(Lines &lines, const Vec3 &p, float r, DWORD color) {
    add_loop(lines, {p.x + r, p.y, p.z}, {p.x, p.y, p.z + r}, {p.x - r, p.y, p.z}, {p.x, p.y, p.z - r}, color);
}

void add_square(Lines &lines, const Vec3 &p, float r, DWORD color) {
    add_loop(lines, {p.x + r, p.y, p.z + r}, {p.x - r, p.y, p.z + r}, {p.x - r, p.y, p.z - r}, {p.x + r, p.y, p.z - r}, color);
}

void draw_lines(IDirect3DDevice9 *device, const Lines &lines) {
    if (lines.empty()) return;
    IDirect3DStateBlock9 *saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
    saved->Capture();
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);  // behind a wall the lines are hidden (the game's depth buffer)
    device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    const auto draw_blended = [&](const std::vector<Vertex> &triangles) {
        if (triangles.empty()) return;
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(triangles.size() / 3), triangles.data(), sizeof(Vertex));
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    };
    draw_blended(lines.fills());
    if (!lines.vertices().empty()) device->DrawPrimitiveUP(D3DPT_LINELIST, static_cast<UINT>(lines.vertices().size() / 2), lines.vertices().data(), sizeof(Vertex));
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    draw_blended(lines.rects());
    if (!lines.top_vertices().empty()) device->DrawPrimitiveUP(D3DPT_LINELIST, static_cast<UINT>(lines.top_vertices().size() / 2), lines.top_vertices().data(), sizeof(Vertex));
    saved->Apply();
    saved->Release();
}

}  // namespace improved_ai_opponents::canvas

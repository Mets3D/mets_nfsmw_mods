/*
 * The game draws the menus and HUD (FEng) once per frame, package by package (cFEngGameInterface::BeginPackageRendering 0x5166A0 and
 * EndPackageRendering 0x516710 around each, RenderObject 0x516630 for each object), after the 3D pictures. Their polygons are made in the screen's own
 * coordinates (FE units, 640 x 480, turned into -1 to 1 by eViewPlatInterface::Render 0x6D1260) and drawn at once, so the D3D viewport decides where on
 * the screen they land: with the viewport set to a part of the screen while a HUD package is drawn, the HUD (made for the whole screen) is drawn scaled
 * into that part, and everything else is drawn as before.
 * A HUD is made for one of two sizes of screen: the whole screen (the HUD of a game with one player: HUD_SingleRace.fng and the like) or the top half of it
 * (the split screen HUDs, HUD_Player1.fng, which every player of a split screen race gets a copy of: the race information at the top right of the half, the
 * speedometer at its bottom right, the minimap on the line between the halves). eViewPlatInterface::Render turns a polygon's y into -(y / 240 - 1.0): the
 * 1.0 is read from the game's constant at 0x89096C in four places (0x6D12D9 and three more, 6 bytes each: fsub dword ptr [0x89096C]), which are pointed
 * at a variable of ours instead, so a picture can be moved up and down within its viewport.
 * Each object is drawn with a viewport of its own: its part of the HUD (a corner, or the middle) decides where the viewport is anchored in the player's cell
 * of the screen and the player's HUD scale how big it is, so the parts scale separately, each around its own corner of the cell. The viewport is the size of
 * the whole picture at that scale, put where the edge of the layout (of the screen or of its top half) comes to the edge of the cell, and moved inside the
 * screen if it would stick out (the origin then makes up for it). A cell that is half the screen or more shows the HUD as it is (a scale of 100%), a smaller
 * one shows it smaller (layout.h).
 */
#include "hud_views.h"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "fe_objects.h"
#include "hooks.h"
#include "layout.h"
#include "max_players.h"
#include "patch.h"
#include "split_screen_support.h"

namespace split_screen_support::hud_views {

namespace {

using namespace fe;

constexpr uintptr_t kDevice = 0x982BDC;  // IDirect3DDevice9 *
constexpr int kMaxPlayers = nfsmw::kMaxPlayers, kMaxHuds = 2 * kMaxPlayers;  // (a HUD each, and the one-player HUDs of a minimap)

enum Layout : unsigned char { kScreenLayout, kHalfLayout };  // what the HUD package is made for: the whole screen, or its top half

struct Assignment {
    char package[48];
    int player, players;
    Layout layout;
};
Assignment g_assigned[kMaxHuds];
int g_count = 0;
struct Scales {
    int percent[kMaxPlayers];
    Scales() { std::fill(percent, percent + kMaxPlayers, 100); }
} g_scales;
int *const g_scale = g_scales.percent;

// The part of the HUD an object is in: a column (left, centre, right) and a row (top, middle, bottom).
enum Column : unsigned char { kLeft, kCenter, kRight };
enum Row : unsigned char { kTop, kMiddle, kBottom };
constexpr unsigned char part_of(Column column, Row row) { return static_cast<unsigned char>(column | (row << 2)); }
constexpr unsigned char kHidden = 0xFF, kDivider = 0xFE;  // not drawn; drawn as the screen has it
std::unordered_map<const void *, unsigned char> g_part;
constexpr unsigned kDividerLine = 0x92D6630D;  // the line between the two halves of the screen of the split screen HUD

float g_origin_y = 1.0f;  // what the game's 1.0 is for the polygon's y while an object is drawn
constexpr uintptr_t kOriginSites[] = {0x6D12D9, 0x6D1329, 0x6D13AC, 0x6D1432};
constexpr float kLayoutHeight = 480.0f;  // FE units of the screen's height

bool g_modified = false;                 // the viewport is a part of the screen, and g_saved is what it was
const Assignment *g_current = nullptr;   // the HUD being drawn
int g_viewport_part = -1;                // the part the viewport was last set for
D3DVIEWPORT9 g_saved;

void(__fastcall *g_orig_begin)(void *, void *, void *);
void(__fastcall *g_orig_end)(void *, void *, void *);
void(__fastcall *g_orig_render_object)(void *, void *, void *);

// What an object that is not known (made after the HUD was) gets: the middle of the layout's bottom (of the screen's middle), where a cell's picture is centred.
unsigned char default_part(const Assignment &a) { return part_of(kCenter, a.layout == kHalfLayout ? kBottom : kMiddle); }

const Assignment *find(const char *name) {
    for (int i = 0; name && i < g_count; ++i)
        if (_stricmp(g_assigned[i].package, name) == 0) return &g_assigned[i];
    return nullptr;
}

IDirect3DDevice9 *device() { return *reinterpret_cast<IDirect3DDevice9 **>(kDevice); }

void restore_viewport() {
    g_current = nullptr;
    g_viewport_part = -1;
    if (!g_modified) return;
    g_modified = false;
    g_origin_y = 1.0f;
    if (auto *d = device()) d->SetViewport(&g_saved);
}

// The viewport for a part of a HUD. Its size is the whole picture at the scale the HUD has here; its horizontal place follows from the part's column (the
// layout's left, middle or right edge at the cell's), its vertical place from the row (top, bottom or middle), kept inside the screen. The origin is what
// makes the layout's edge land on the cell's edge wherever the viewport had to be put.
void set_viewport(const Assignment &a, unsigned char part) {
    IDirect3DDevice9 *d = device();
    if (!d) return;
    const long width = g_saved.Width, height = g_saved.Height;
    D3DVIEWPORT9 view = g_saved;
    g_viewport_part = part;
    if (part == kDivider) {  // the screen's own picture
        g_modified = SUCCEEDED(d->SetViewport(&view));
        g_origin_y = 1.0f;
        return;
    }
    const RECT cell = layout::cell(a.player, a.players, width, height);
    const float factor = std::fmin(1.0f, std::fmax(0.3f, g_scale[a.player < kMaxPlayers ? a.player : 0] / 100.0f)) * layout::hud_base_scale(cell, width, height);
    const long view_width = std::lround(width * factor), view_height = std::lround(height * factor);
    const Column column = static_cast<Column>(part & 3);
    const Row row = static_cast<Row>(part >> 2);
    const float layout_bottom = a.layout == kHalfLayout ? kLayoutHeight / 2 : kLayoutHeight;  // the layout spans y 0 to this

    const long left = column == kLeft ? cell.left : column == kRight ? cell.right - view_width : (cell.left + cell.right - view_width) / 2;
    view.X = g_saved.X + std::clamp<long>(left, 0, width - view_width);
    // The layout's edge (FE units, y down) and the pixel of the screen it has to come to.
    const float anchor = row == kTop ? 0.0f : row == kBottom ? layout_bottom : layout_bottom / 2;
    const long pixel = row == kTop ? cell.top : row == kBottom ? cell.bottom : (cell.top + cell.bottom) / 2;
    const long wanted_top = row == kTop ? cell.top : row == kBottom ? cell.bottom - view_height : pixel - view_height / 2;
    const long top = std::clamp<long>(wanted_top, 0, height - view_height);
    view.Y = g_saved.Y + top;
    view.Width = view_width;
    view.Height = view_height;
    g_modified = SUCCEEDED(d->SetViewport(&view));
    // -(y / 240 - origin) is the viewport's y (1 at its top, -1 at its bottom) of the layout's y.
    const float at = 1.0f - 2.0f * static_cast<float>(pixel - top) / static_cast<float>(view_height);
    g_origin_y = g_modified ? at + anchor / (kLayoutHeight / 2) : 1.0f;
}

void __fastcall hook_begin(void *self, void *edx, void *package) {
    g_orig_begin(self, edx, package);
    restore_viewport();
    const Assignment *a = find(package ? package_name(package) : nullptr);
    IDirect3DDevice9 *d = device();
    if (!a || !d || FAILED(d->GetViewport(&g_saved))) return;
    g_current = a;
    set_viewport(*a, default_part(*a));
}

void __fastcall hook_end(void *self, void *edx, void *package) {
    g_orig_end(self, edx, package);
    restore_viewport();
}

void __fastcall hook_render_object(void *self, void *edx, void *object) {
    if (g_current && object) {
        const auto found = g_part.find(object);
        if (found != g_part.end()) {
            if (found->second == kHidden) return;
            if (found->second != g_viewport_part) set_viewport(*g_current, found->second);
        }
    }
    g_orig_render_object(self, edx, object);
}

// Which part of the HUD: left of x -100 (the minimap and what goes with it: always the bottom), right of x 100, else the middle. FE units, the middle of the
// screen is (0, 0). A layout of the top half of the screen (y -240 to 0) has a top part above y -150 (the race information, the countdown and messages) and a
// bottom part under it (the speedometer, the minimap); a layout of the whole screen a top part above y -60 on the right (the race information) and above
// -120 in the middle (the bar at the top), a bottom part under y 120 in the middle (and everything else on the right), and a middle part between.
unsigned char part_at(Layout layout, float x, float y) {
    const Column column = x < -100.0f ? kLeft : x > 100.0f ? kRight : kCenter;
    if (column == kLeft) return part_of(column, kBottom);
    if (layout == kHalfLayout) return part_of(column, y < -150.0f ? kTop : kBottom);
    if (column == kRight) return part_of(column, y < -60.0f ? kTop : kBottom);
    return part_of(column, y < -120.0f ? kTop : y > 120.0f ? kBottom : kMiddle);
}

// The object's place is its own plus its groups' (no scaling or turning of groups is taken into account: the HUDs' groups have none).
void classify_object(const Assignment &a, void *object, float offset_x, float offset_y, int depth) {
    if (!field<void *>(object, 0x2C)) return;
    const float x = offset_x + position_x(object), y = offset_y + position_y(object);
    if (hash_of(object) == kDividerLine) {
        // Drawn as the screen has it, by the first player of two or three (the line is between the halves); not at all with four or more.
        g_part[object] = a.player == 0 && a.players >= 2 && a.players <= 3 ? kDivider : kHidden;
        return;
    }
    if (is_group(object) && depth < 8) {
        for (void *child = first_child(object); child; child = next_of(child)) classify_object(a, child, x, y, depth + 1);
        return;
    }
    g_part[object] = part_at(a.layout, x, y);
}

}  // namespace

void assign(const char *package, int player, int players) {
    Assignment *a = nullptr;
    for (int i = 0; i < g_count && !a; ++i)
        if (_stricmp(g_assigned[i].package, package) == 0) a = &g_assigned[i];
    if (!a) {
        if (g_count >= kMaxHuds) return;
        a = &g_assigned[g_count++];
        strncpy(a->package, package, sizeof(a->package) - 1);
        a->package[sizeof(a->package) - 1] = 0;
    }
    a->player = player;
    a->players = players;
    a->layout = strncmp(package, "HUD_Player", 10) == 0 || strncmp(package, "HUD_Drag_Player", 15) == 0 ? kHalfLayout : kScreenLayout;
    g_log.line("hud: %s is drawn in the cell of player %d of %d (a HUD for %s)", package, player + 1, players, a->layout == kHalfLayout ? "half a screen" : "the screen");
}

void classify(const char *package) {
    void *p = find_package(package);
    const Assignment *a = find(package);
    if (!p || !a) return;
    for (void *object = first_object(p); object; object = next_of(object)) classify_object(*a, object, 0.0f, 0.0f, 0);
}

void classify_as_minimap(void *object) {
    if (!object) return;
    g_part[object] = part_of(kLeft, kBottom);
    if (!is_group(object)) return;
    for (void *child = first_child(object); child; child = next_of(child)) classify_as_minimap(child);
}

int part_of_object(const void *object) {
    const auto found = g_part.find(object);
    return found == g_part.end() ? -1 : found->second;
}

void classify_like(void *object, void *model) {
    const auto found = g_part.find(model);
    if (found != g_part.end()) g_part[object] = found->second;
}

void set_scale(int player, int percent) {
    if (player >= 0 && player < kMaxPlayers) g_scale[player] = percent < 30 ? 30 : percent > 100 ? 100 : percent;
}

int scale(int player) { return player >= 0 && player < kMaxPlayers ? g_scale[player] : 100; }

void clear() {
    restore_viewport();
    g_count = 0;
    g_part.clear();
}

void install() {
    const nfsmw::Hook hooks[] = {
        {"hudbegin", "cFEngGameInterface::BeginPackageRendering", 0x5166A0, {0x55, 0x8B, 0x6C, 0x24, 0x08, 0x56, 0x8B}, false, nfsmw::detour(&hook_begin),
         nfsmw::orig(&g_orig_begin)},
        {"hudend", "cFEngGameInterface::EndPackageRendering", 0x516710, {0x8B, 0x0D, 0xA8, 0xCF, 0x91, 0x00, 0xE9}, false, nfsmw::detour(&hook_end),
         nfsmw::orig(&g_orig_end)},
        {"hudobject", "cFEngGameInterface::RenderObject", 0x516630, {0x53, 0x56, 0x8B, 0x74, 0x24, 0x0C, 0x8B}, false, nfsmw::detour(&hook_render_object),
         nfsmw::orig(&g_orig_render_object)},
    };
    nfsmw::install_hooks(hooks, 3, g_log, nullptr, "hud views");
    int patched = 0;
    for (const uintptr_t site : kOriginSites) {
        const unsigned char expected[6] = {0xD8, 0x25, 0x6C, 0x09, 0x89, 0x00};
        unsigned char replacement[6] = {0xD8, 0x25};
        const uintptr_t variable = reinterpret_cast<uintptr_t>(&g_origin_y);
        memcpy(replacement + 2, &variable, 4);
        patched += nfsmw::patch_bytes(site, expected, replacement, sizeof(expected)) ? 1 : 0;
    }
    g_log.line("hud views: %d of 4 places in the polygon code point at the HUD's vertical origin", patched);
}

}  // namespace split_screen_support::hud_views

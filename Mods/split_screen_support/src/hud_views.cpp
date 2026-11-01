/*
 * The game draws the menus and HUD (FEng) once per frame, package by package (cFEngGameInterface::BeginPackageRendering 0x5166A0 and
 * EndPackageRendering 0x516710 around each, RenderObject 0x516630 for each object), after the 3D pictures. Their polygons are made in the screen's own
 * coordinates (FE units, 640 x 480) and drawn at once. eViewPlatInterface::Render 0x6D1260 turns a polygon's x into x * [0x8AF9A4] - [0x89096C] and its
 * y into -(y * [0x8AF9A0] - [0x89096C]) (1 / 320, 1.0 and 1 / 240; the widescreen fix changes the x scale, which keeps the menus and HUD 4:3). The eight
 * reads of the 1.0 (two per corner) are pointed at variables of ours, and the two scales are written while a HUD object is drawn and put back after it, so
 * a HUD's polygons can be put anywhere at any size, and everything else is drawn as before.
 * A HUD is made for one of two sizes of screen: the whole screen (the HUD of a game with one player: HUD_SingleRace.fng and the like) or the top half of it
 * (the split screen HUDs, HUD_Player1.fng, which every player of a split screen race gets a copy of: the race information at the top right of the half, the
 * speedometer at its bottom right, the minimap on the line between the halves).
 * Each object is drawn as if the whole layout were drawn into a picture of its own, the size of the screen at the HUD's scale: its part of the HUD (a corner,
 * or the middle) decides where that picture is anchored in the player's cell of the screen (the layout's edge, of the screen or of its top half, at the
 * cell's edge), and the player's HUD scale how big it is, so the parts scale separately, each around its own corner of the cell. The D3D viewport is the cell
 * (a viewport has to lie inside the screen, which a picture bigger than the cell would not) and the variables map the picture into it, so a HUD can be bigger
 * than 100% and is cut at the edge of its cell. A cell that is half the screen or more shows the HUD as it is (a scale of 100%), a smaller one shows it
 * smaller (layout.h).
 * The rear view mirror's picture is no FEng object: eDisplayFrame_RenderFrontEnd (0x6E6E40) draws it as one polygon (FE units x 190 to 450, y 19 to 89, the mirror's texture) with
 * eViewPlatInterface::Render (its call at 0x6E7199) before the menus and HUD, under the HUD's mirror frame (top middle). That call gets the viewport of the
 * frame's part of each player's HUD, so the picture scales with the frame. That part, kMirror, is the HUD's top middle at the mirror's size (layout::mirror_scale).
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
#include "mirrors.h"
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
constexpr unsigned char kMirrorBit = 0x10;                // the top middle, at the size of the rear view mirror (layout::mirror_scale)
constexpr unsigned char kMirror = part_of(kCenter, kTop) | kMirrorBit;
std::unordered_map<const void *, unsigned char> g_part;
constexpr unsigned kDividerLine = 0x92D6630D;  // the line between the two halves of the screen of the split screen HUD

// The polygon transform while an object is drawn: the origins are ours (1.0 otherwise), the scales the game's constants, saved before we change them.
constexpr uintptr_t kGameScaleX = 0x8AF9A4, kGameScaleY = 0x8AF9A0;
float &game_scale(uintptr_t at) { return *reinterpret_cast<float *>(at); }
float g_origin_x = 1.0f, g_origin_y = 1.0f;
float g_saved_scale_x = 0, g_saved_scale_y = 0;
bool g_scales_saved = false;
struct Site {
    uintptr_t at;
    float *variable;
};
const Site kSites[] = {  // fsub dword ptr [0x89096C]
    {0x6D12C6, &g_origin_x}, {0x6D1310, &g_origin_x}, {0x6D1393, &g_origin_x}, {0x6D1416, &g_origin_x},
    {0x6D12D9, &g_origin_y}, {0x6D1329, &g_origin_y}, {0x6D13AC, &g_origin_y}, {0x6D1432, &g_origin_y},
};
constexpr float kLayoutHeight = 480.0f;  // FE units of the screen's height

// The game's transform again.
void reset_transform() {
    g_origin_x = g_origin_y = 1.0f;
    if (!g_scales_saved) return;
    g_scales_saved = false;
    game_scale(kGameScaleX) = g_saved_scale_x;
    game_scale(kGameScaleY) = g_saved_scale_y;
}

bool g_modified = false;                 // the viewport is a part of the screen, and g_saved is what it was
const Assignment *g_current = nullptr;   // the HUD being drawn
int g_viewport_part = -1;                // the part the viewport was last set for
bool g_no_cell = false;                  // the player's cell has no width now (layout.h: they are out): their HUD is not drawn
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
    g_no_cell = false;
    if (!g_modified) return;
    g_modified = false;
    reset_transform();
    if (auto *d = device()) d->SetViewport(&g_saved);
}

// The viewport for a part of a HUD: the player's cell, and the transform that puts the picture in it. The picture is the whole layout at the scale the HUD has
// here; its horizontal place follows from the part's column (the layout's left, middle or right edge at the cell's), its vertical place from the row (top,
// bottom or middle); it may be bigger than the cell, which cuts it.
void set_viewport(const Assignment &a, unsigned char part) {
    IDirect3DDevice9 *d = device();
    if (!d) return;
    const long width = g_saved.Width, height = g_saved.Height;
    D3DVIEWPORT9 view = g_saved;
    g_viewport_part = part;
    reset_transform();
    const float scale_x = game_scale(kGameScaleX), scale_y = game_scale(kGameScaleY);
    if (part == kDivider) {  // the screen's own picture
        g_modified = SUCCEEDED(d->SetViewport(&view));
        return;
    }
    const RECT cell = layout::cell(a.player, a.players, width, height);
    g_no_cell = cell.right <= cell.left || cell.bottom <= cell.top;
    if (g_no_cell) return;
    const float percent = static_cast<float>(g_scale[a.player < kMaxPlayers ? a.player : 0]);
    const float cell_scale = part & kMirrorBit ? layout::mirror_scale(a.player, a.players, width, height) : layout::hud_scale(a.player, a.players, width, height);
    const float factor = std::clamp(percent / 100.0f, 0.3f, kMaxScale / 100.0f) * cell_scale;
    const float picture_width = width * factor, picture_height = height * factor;
    const Column column = static_cast<Column>(part & 3);
    const Row row = static_cast<Row>((part & ~kMirrorBit) >> 2);
    const float layout_bottom = a.layout == kHalfLayout ? kLayoutHeight / 2 : kLayoutHeight;  // the layout spans y 0 to this

    // The picture's place (pixels of the screen; it may stick out of the cell and of the screen).
    const float left = column == kLeft ? cell.left : column == kRight ? cell.right - picture_width : (cell.left + cell.right - picture_width) / 2;
    const float anchor = row == kTop ? 0.0f : row == kBottom ? layout_bottom : layout_bottom / 2;  // the layout's edge, FE units from the top
    const float pixel = row == kTop ? cell.top : row == kBottom ? cell.bottom : (cell.top + cell.bottom) / 2.0f;  // where it comes to
    const float top = pixel - anchor / kLayoutHeight * picture_height;

    view.X = g_saved.X + cell.left;
    view.Y = g_saved.Y + cell.top;
    view.Width = cell.right - cell.left;
    view.Height = cell.bottom - cell.top;
    g_modified = SUCCEEDED(d->SetViewport(&view));
    if (!g_modified) return;
    // The game's transform puts the layout over the viewport (its -1..1); this one puts the game's -1..1 over the picture's rectangle, in the cell's -1..1:
    // shrunk by the picture's size over the cell's, moved to the picture's centre.
    const float cell_width = static_cast<float>(view.Width), cell_height = static_cast<float>(view.Height);
    const float along_x = picture_width / cell_width, along_y = picture_height / cell_height;
    const float centre_x = (2.0f * (left - cell.left) + picture_width) / cell_width - 1.0f;
    const float centre_y = 1.0f - (2.0f * (top - cell.top) + picture_height) / cell_height;
    g_saved_scale_x = scale_x;
    g_saved_scale_y = scale_y;
    g_scales_saved = true;
    game_scale(kGameScaleX) = scale_x * along_x;
    game_scale(kGameScaleY) = scale_y * along_y;
    g_origin_x = along_x - centre_x;
    g_origin_y = along_y + centre_y;
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
        if (g_no_cell) return;
        const auto found = g_part.find(object);
        if (found != g_part.end()) {
            if (found->second == kHidden || (found->second == kDivider && !layout::game_halves(g_current->players))) return;
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

// The mirror's polygon: eViewPlatInterface::Render(ePoly *, int, int), cdecl.
constexpr uintptr_t kMirrorRenderCall = 0x6E7199;
uintptr_t g_render_poly = 0;
// A split screen race: the polygon once for each player who has a mirror (mirrors.cpp), in the top middle of their HUD. False when none has (a game of one player).
bool render_player_mirrors(void *poly, int a, int b) {
    IDirect3DDevice9 *d = device();
    bool any = false;
    for (int i = 0; i < g_count && d; ++i) {
        const Assignment &hud = g_assigned[i];
        if (hud.players < 2 || !mirrors::bind(hud.player) || FAILED(d->GetViewport(&g_saved))) continue;
        set_viewport(hud, kMirror);
        if (!g_no_cell) reinterpret_cast<void(__cdecl *)(void *, int, int)>(g_render_poly)(poly, a, b);
        restore_viewport();
        any = true;
    }
    return any;
}

void __cdecl render_mirror(void *poly, int a, int b) {
    if (render_player_mirrors(poly, a, b)) return;
    const Assignment *hud = nullptr;
    for (int i = 0; i < g_count && !hud; ++i)
        if (g_assigned[i].player == 0 && g_assigned[i].layout == kScreenLayout) hud = &g_assigned[i];
    IDirect3DDevice9 *d = device();
    const bool scaled = hud && d && SUCCEEDED(d->GetViewport(&g_saved));
    if (scaled) set_viewport(*hud, part_of(kCenter, kTop));
    reinterpret_cast<void(__cdecl *)(void *, int, int)>(g_render_poly)(poly, a, b);
    if (scaled) restore_viewport();
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

void classify_as_mirror(void *object) {
    if (!object) return;
    g_part[object] = kMirror;
    if (!is_group(object)) return;
    for (void *child = first_child(object); child; child = next_of(child)) classify_as_mirror(child);
}

int part_of_object(const void *object) {
    const auto found = g_part.find(object);
    return found == g_part.end() ? -1 : found->second;
}

void classify_like(void *object, void *model) {
    const auto found = g_part.find(model);
    if (found != g_part.end()) g_part[object] = found->second;
}

namespace {
int part_of_tree(void *object, int depth = 0) {
    const auto found = g_part.find(object);
    if (found != g_part.end()) return found->second;
    if (!is_group(object) || depth > 8) return -1;
    for (void *child = first_child(object); child; child = next_of(child))
        if (const int part = part_of_tree(child, depth + 1); part >= 0) return part;
    return -1;
}

void set_tree(void *object, unsigned char part, int depth = 0) {
    g_part[object] = part;
    if (is_group(object) && depth < 8)
        for (void *child = first_child(object); child; child = next_of(child)) set_tree(child, part, depth + 1);
}
}  // namespace

void classify_tree_like(void *object, void *model) {
    const int part = object && model ? part_of_tree(model) : -1;
    if (part >= 0) set_tree(object, static_cast<unsigned char>(part));
}

void set_scale(int player, int percent) {
    if (player >= 0 && player < kMaxPlayers) g_scale[player] = std::clamp(percent, 30, kMaxScale);
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
    for (const Site &site : kSites) {
        const unsigned char expected[6] = {0xD8, 0x25, 0x6C, 0x09, 0x89, 0x00};
        unsigned char replacement[6] = {0xD8, 0x25};
        const uintptr_t variable = reinterpret_cast<uintptr_t>(site.variable);
        memcpy(replacement + 2, &variable, 4);
        patched += nfsmw::patch_bytes(site.at, expected, replacement, sizeof(expected)) ? 1 : 0;
    }
    DWORD old = 0;  // the scales are written while a HUD object is drawn
    const bool writable = VirtualProtect(reinterpret_cast<void *>(kGameScaleY), 8, PAGE_READWRITE, &old);
    g_log.line("hud views: %d of 8 places in the polygon code point at the HUD's origin%s", patched, writable ? "" : "; the scales could not be made writable");
    g_render_poly = nfsmw::chain_call(kMirrorRenderCall, reinterpret_cast<uintptr_t>(&render_mirror));
    if (!g_render_poly) g_log.line("hud views: the call at %#x is not as expected; the rear view mirror's picture keeps its size", static_cast<unsigned>(kMirrorRenderCall));
}

}  // namespace split_screen_support::hud_views

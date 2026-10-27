/*
 * The game draws the menus and HUD (FEng) once per frame, package by package (cFEngGameInterface::BeginPackageRendering 0x5166A0 and
 * EndPackageRendering 0x516710 around each, RenderObject 0x516630 for each object), after the 3D pictures. Their polygons are made in the screen's own
 * coordinates (FE units, 640 x 480, turned into -1 to 1 by eViewPlatInterface::Render 0x6D1260) and drawn at once, so the D3D viewport decides where on
 * the screen they land: with the viewport set to a player's cell for that player's HUD package, the HUD (made for the whole screen) is drawn scaled
 * down into the cell, and everything else is drawn as before.
 * The HUDs are made for the top half of the screen (HUD_Player1.fng; see hud_players.cpp): the race information at its top right, the speedometer at its
 * bottom right (the middle of the screen's right side), the minimap on the line between the halves. eViewPlatInterface::Render turns a polygon's y into
 * -(y / 240 - 1.0): the 1.0 is read from the game's constant at 0x89096C in four places (0x6D12D9 and three more, 6 bytes each: fsub dword ptr
 * [0x89096C]), which are pointed at a variable of ours instead. With 0 in it an object is drawn one viewport lower, so the top half of the screen's
 * picture is the bottom half of the cell (and what would be below it is outside the viewport).
 * Each object is drawn with a viewport of its own: its corner decides where the viewport is anchored in the cell (a top corner keeps the 1.0, a bottom
 * corner takes the shift) and the player's HUD scale how big it is, so the corners scale separately, each around its own corner of the cell.
 */
#include "hud_views.h"

#include <d3d9.h>

#include <cmath>
#include <cstring>
#include <unordered_map>

#include "fe_objects.h"
#include "hooks.h"
#include "layout.h"
#include "patch.h"
#include "split_screen_support.h"

namespace split_screen_support::hud_views {

namespace {

using namespace fe;

constexpr uintptr_t kDevice = 0x982BDC;  // IDirect3DDevice9 *
constexpr int kMaxHuds = 16, kMaxPlayers = 8;

struct Assignment {
    char package[48];
    int player, players;
};
Assignment g_assigned[kMaxHuds];
int g_count = 0;
int g_scale[kMaxPlayers] = {100, 100, 100, 100, 100, 100, 100, 100};

// A corner of the HUD: horizontal anchor 0 left, 1 centre, 2 right; vertical 0 top, 1 bottom.
enum Region : unsigned char { kLeftTop, kLeftBottom, kCenterTop, kCenterBottom, kRightTop, kRightBottom, kHidden = 0xFF };
std::unordered_map<const void *, unsigned char> g_region;
constexpr unsigned kDividerLine = 0x92D6630D;  // the line between the two halves of the screen of the two player HUD: not wanted in a cell

float g_origin_y = 1.0f;  // what the game's 1.0 is for the polygon's y while an object is drawn
constexpr uintptr_t kOriginSites[] = {0x6D12D9, 0x6D1329, 0x6D13AC, 0x6D1432};

bool g_modified = false;                 // the viewport is a cell's (or part of it), and g_saved is what it was
const Assignment *g_current = nullptr;   // the HUD being drawn
int g_viewport_region = -1;              // the region the viewport was last set for
D3DVIEWPORT9 g_saved;

void(__fastcall *g_orig_begin)(void *, void *, void *);
void(__fastcall *g_orig_end)(void *, void *, void *);
void(__fastcall *g_orig_render_object)(void *, void *, void *);

const Assignment *find(const char *name) {
    for (int i = 0; name && i < g_count; ++i)
        if (_stricmp(g_assigned[i].package, name) == 0) return &g_assigned[i];
    return nullptr;
}

IDirect3DDevice9 *device() { return *reinterpret_cast<IDirect3DDevice9 **>(kDevice); }

void restore_viewport() {
    g_current = nullptr;
    g_viewport_region = -1;
    if (!g_modified) return;
    g_modified = false;
    g_origin_y = 1.0f;
    if (auto *d = device()) d->SetViewport(&g_saved);
}

// The viewport of a corner of a HUD: the cell scaled by the player's HUD scale, anchored to that corner of the cell.
void set_viewport(const Assignment &a, int region) {
    IDirect3DDevice9 *d = device();
    if (!d) return;
    const RECT cell = layout::cell(a.player, a.players, g_saved.Width, g_saved.Height);
    const long width = cell.right - cell.left, height = cell.bottom - cell.top;
    const float factor = std::fmin(1.0f, std::fmax(0.3f, g_scale[a.player < kMaxPlayers ? a.player : 0] / 100.0f));
    const long view_width = std::lround(width * factor), view_height = std::lround(height * factor);
    const bool left = region == kLeftTop || region == kLeftBottom, right = region == kRightTop || region == kRightBottom;
    const bool top = region == kLeftTop || region == kCenterTop || region == kRightTop;
    D3DVIEWPORT9 view = g_saved;
    view.X = g_saved.X + cell.left + (left ? 0 : right ? width - view_width : (width - view_width) / 2);
    view.Y = g_saved.Y + cell.top + (top ? 0 : height - view_height);
    view.Width = view_width;
    view.Height = view_height;
    g_modified = SUCCEEDED(d->SetViewport(&view));
    g_origin_y = g_modified && !top ? 0.0f : 1.0f;
    g_viewport_region = region;
}

void __fastcall hook_begin(void *self, void *edx, void *package) {
    g_orig_begin(self, edx, package);
    restore_viewport();
    const Assignment *a = find(package ? package_name(package) : nullptr);
    IDirect3DDevice9 *d = device();
    if (!a || !d || FAILED(d->GetViewport(&g_saved))) return;
    g_current = a;
    set_viewport(*a, kRightBottom);  // what an object without a corner gets: the whole cell, drawn low
}

void __fastcall hook_end(void *self, void *edx, void *package) {
    g_orig_end(self, edx, package);
    restore_viewport();
}

void __fastcall hook_render_object(void *self, void *edx, void *object) {
    if (g_current && object) {
        const auto found = g_region.find(object);
        if (found != g_region.end()) {
            if (found->second == kHidden) return;
            if (found->second != g_viewport_region) set_viewport(*g_current, found->second);
        }
    }
    g_orig_render_object(self, edx, object);
}

// Which corner: left of x -100 (the minimap and its markers), right of x 100, else the middle; above y -150 the top (the race information, the countdown and
// messages), else the bottom. FE units, the middle of the screen is (0, 0). The object's place is its own plus its groups' (no scaling or turning of groups
// is taken into account: the HUD's groups have none).
unsigned char region_of(float x, float y) {
    if (x < -100.0f) return kLeftBottom;
    const bool top = y < -150.0f;
    if (x > 100.0f) return top ? kRightTop : kRightBottom;
    return top ? kCenterTop : kCenterBottom;
}

void classify_object(void *object, float offset_x, float offset_y, int depth) {
    if (!field<void *>(object, 0x2C)) return;
    const float x = offset_x + position_x(object), y = offset_y + position_y(object);
    if (hash_of(object) == kDividerLine) {
        g_region[object] = kHidden;
        return;
    }
    if (is_group(object) && depth < 8) {
        for (void *child = first_child(object); child; child = next_of(child)) classify_object(child, x, y, depth + 1);
        return;
    }
    g_region[object] = region_of(x, y);
}

}  // namespace

void assign(const char *package, int player, int players) {
    for (int i = 0; i < g_count; ++i)
        if (_stricmp(g_assigned[i].package, package) == 0) {
            g_assigned[i].player = player;
            g_assigned[i].players = players;
            return;
        }
    if (g_count >= kMaxHuds) return;
    Assignment &a = g_assigned[g_count++];
    strncpy(a.package, package, sizeof(a.package) - 1);
    a.package[sizeof(a.package) - 1] = 0;
    a.player = player;
    a.players = players;
    g_log.line("hud: %s is drawn in the cell of player %d of %d", package, player + 1, players);
}

void classify(const char *package) {
    void *p = find_package(package);
    if (!p) return;
    for (void *object = first_object(p); object; object = next_of(object)) classify_object(object, 0.0f, 0.0f, 0);
}

void classify_like(void *object, void *model) {
    const auto found = g_region.find(model);
    if (found != g_region.end()) g_region[object] = found->second;
}

void set_scale(int player, int percent) {
    if (player >= 0 && player < kMaxPlayers) g_scale[player] = percent < 30 ? 30 : percent > 100 ? 100 : percent;
}

int scale(int player) { return player >= 0 && player < kMaxPlayers ? g_scale[player] : 100; }

void clear() {
    restore_viewport();
    g_count = 0;
    g_region.clear();
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

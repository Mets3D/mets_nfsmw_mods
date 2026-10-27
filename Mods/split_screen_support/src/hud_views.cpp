/*
 * The game draws the menus and HUD (FEng) once per frame, package by package (cFEngGameInterface::BeginPackageRendering 0x5166A0 and
 * EndPackageRendering 0x516710 around each), after the 3D pictures. Their polygons are made in the screen's own coordinates (FE units, 640 x 480,
 * turned into -1 to 1 by eViewPlatInterface::Render 0x6D1260) and drawn at once, so the D3D viewport decides where on the screen they land: with the
 * viewport set to a player's cell for that player's HUD package, the HUD (made for the whole screen) is drawn scaled down into the cell, and
 * everything else is drawn as before.
 * The HUDs are made for the top half of the screen (HUD_Player1.fng; see hud_players.cpp), which put the speedometer in the middle of a cell's right
 * side. eViewPlatInterface::Render turns a polygon's y into -(y / 240 - 1.0): the 1.0 is read from the game's constant at 0x89096C in four places
 * (0x6D12D9 and three more, 6 bytes each: fsub dword ptr [0x89096C]), which are pointed at a variable of ours instead. With 0 in it a HUD is drawn one
 * viewport lower, so the top half of the screen's picture is the bottom half of the cell (and what would be below it is outside the viewport).
 */
#include "hud_views.h"

#include <d3d9.h>

#include <cstring>

#include "hooks.h"
#include "layout.h"
#include "patch.h"
#include "split_screen_support.h"

namespace split_screen_support::hud_views {

namespace {

constexpr uintptr_t kDevice = 0x982BDC;  // IDirect3DDevice9 *
constexpr int kPackageName = 0xC;        // FEPackage: name (char *)
constexpr int kMaxHuds = 16;

struct Assignment {
    char package[48];
    int player, players;
};
Assignment g_assigned[kMaxHuds];
int g_count = 0;

float g_origin_y = 1.0f;  // what the game's 1.0 is for the polygon's y while a HUD is drawn
constexpr uintptr_t kOriginSites[] = {0x6D12D9, 0x6D1329, 0x6D13AC, 0x6D1432};

bool g_modified = false;  // the viewport is a cell's, and g_saved is what it was
D3DVIEWPORT9 g_saved;

void(__fastcall *g_orig_begin)(void *, void *, void *);
void(__fastcall *g_orig_end)(void *, void *, void *);

const Assignment *find(const char *name) {
    for (int i = 0; name && i < g_count; ++i)
        if (_stricmp(g_assigned[i].package, name) == 0) return &g_assigned[i];
    return nullptr;
}

IDirect3DDevice9 *device() { return *reinterpret_cast<IDirect3DDevice9 **>(kDevice); }

void restore_viewport() {
    if (!g_modified) return;
    g_modified = false;
    g_origin_y = 1.0f;
    if (auto *d = device()) d->SetViewport(&g_saved);
}

void __fastcall hook_begin(void *self, void *edx, void *package) {
    g_orig_begin(self, edx, package);
    restore_viewport();
    const Assignment *a = find(package ? *reinterpret_cast<const char **>(static_cast<char *>(package) + kPackageName) : nullptr);
    IDirect3DDevice9 *d = device();
    if (!a || !d || FAILED(d->GetViewport(&g_saved))) return;
    const RECT cell = layout::cell(a->player, a->players, g_saved.Width, g_saved.Height);
    D3DVIEWPORT9 view = g_saved;
    view.X = g_saved.X + cell.left;
    view.Y = g_saved.Y + cell.top;
    view.Width = cell.right - cell.left;
    view.Height = cell.bottom - cell.top;
    g_modified = SUCCEEDED(d->SetViewport(&view));
    if (g_modified) g_origin_y = 0.0f;
}

void __fastcall hook_end(void *self, void *edx, void *package) {
    g_orig_end(self, edx, package);
    restore_viewport();
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

void clear() {
    restore_viewport();
    g_count = 0;
}

void install() {
    const nfsmw::Hook hooks[] = {
        {"hudbegin", "cFEngGameInterface::BeginPackageRendering", 0x5166A0, {0x55, 0x8B, 0x6C, 0x24, 0x08, 0x56, 0x8B}, false, nfsmw::detour(&hook_begin),
         nfsmw::orig(&g_orig_begin)},
        {"hudend", "cFEngGameInterface::EndPackageRendering", 0x516710, {0x8B, 0x0D, 0xA8, 0xCF, 0x91, 0x00, 0xE9}, false, nfsmw::detour(&hook_end),
         nfsmw::orig(&g_orig_end)},
    };
    nfsmw::install_hooks(hooks, 2, g_log, nullptr, "hud views");
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

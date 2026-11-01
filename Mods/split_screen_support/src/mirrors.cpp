/*
 * The rear view mirror of each player of a split screen race. The game draws one, in the view mode 2 of a game of one player: view 3 is on, a RearViewMirrorCameraMover
 * puts its camera behind the car, ___eDisplayFrame draws the world from it into the render target 0x93DB0C (texture [0x982A7C]), and eDisplayFrame_RenderFrontEnd draws
 * that as a polygon at the top of the screen. A split screen race is in the view mode 3, which has none. Each player gets their own mover (in no view's list: moved here),
 * camera, PlatInfo, texture and list of movers (the game leaves the car of the first mover in view 3's list out of the mirror), which view 3 is given for the player's
 * pass (split_view.cpp): the same call of ___eDisplayFrame draws the player's mirror. hud_views.cpp draws the polygon once for each player, hud_copies.cpp gives each HUD
 * the frame. More in agent_docs/split-screen.md.
 * The ini's allow_rear_view_mirror is also a row of Options > Advanced Video (a page of nfsmw_custom_menus, at the end of this file).
 */
#include "mirrors.h"

#include <d3d9.h>

#include <cstdint>
#include <utility>

#include "camera_settings_api.h"
#include "custom_menus_api.h"
#include "events.h"
#include "extra_cameras.h"
#include "max_players.h"
#include "more_players.h"
#include "mover_list.h"
#include "players.h"
#include "split_screen_support.h"
#include "view_ids.h"

namespace split_screen_support::mirrors {

namespace {

constexpr uintptr_t kFEDatabase = 0x91CF90, kView3 = 0x919730, kTarget = 0x93DB0C, kGameTexture = 0x982A7C, kEffectUser = 0x982C80;
constexpr uintptr_t kNew = 0x652AD0, kMoverConstructor = 0x470890, kMoverVtable = 0x894E48, kSetupLighting = 0x5029F0, kSetupPerView = 0x6CF400;
constexpr int kViewPlatInfo = 0x00, kViewActive = 0x08, kViewCamera = 0x40;    // eView
constexpr int kTargetSurface = 0x08, kTargetOn = 0x10;                        // eRenderTarget
constexpr int kMoverViewId = 0x10, kMoverCamera = 0x1C, kMoverAnchor = 0x80;  // RearViewMirrorCameraMover
constexpr size_t kMoverSize = 0x90, kCameraSize = 0x2C0, kPlatInfoSize = 0x1A0;
constexpr unsigned kFarPlane = 0x466A6000;                                   // 15000.0f, as eDisplayFrame passes it
constexpr int kSettingsOfPlayer = 5, kCurCam = 0x0C, kLookBack = 0x30 / 4;   // IPlayer vtable: its PlayerSettings (CurCam at +0xC); CubicCameraMover vtable: looking back
constexpr int kOptions = 0x10, kRearviewOn = 0x39;                           // FEDatabase: the options settings; their Rearview option
constexpr uintptr_t kShouldPauseSimulation = 0x525B20;                       // FEManager::ShouldPauseSimulation(bool), cdecl: what the game's mirror asks too
constexpr int kEffectSetTexture = 0xD0 / 4, kEffectParameters = 0x44, kEffect = 0x48, kTextureHandle = 0x22C;  // ID3DXEffect vtable; the fields of [kEffectUser]

struct Mirror {
    void *camera = nullptr, *plat_info = nullptr, *mover = nullptr;
    ListNode movers = {};  // view 3's list of movers in the pass: this mover alone
    IDirect3DTexture9 *texture = nullptr;
    IDirect3DSurface9 *surface = nullptr;
    bool shown = false;  // the player's camera view has a mirror now
    bool drawn = false;  // its picture was made this frame
};
Mirror g_mirror[nfsmw::kMaxPlayers];
bool g_enabled = true;
bool g_view_on = false;  // view 3 was turned on after the passes, for the polygon: until the next frame
nfsmw::camera_settings_api::MirrorWantedFn g_camera_settings_wanted = nullptr;

template <typename T>
T &at(uintptr_t base, int offset) {
    return *reinterpret_cast<T *>(base + offset);
}
template <typename T>
T &at(void *base, int offset) {
    return at<T>(reinterpret_cast<uintptr_t>(base), offset);
}

void set_view_on(bool on) {
    at<unsigned char>(kView3, kViewActive) = on ? 1 : 0;
    at<int>(kTarget, kTargetOn) = on ? 1 : 0;
}

// What makes view 3 the player's mirror: traded in for the pass, and back after it.
void trade(Mirror &m) {
    std::swap(at<void *>(kView3, kViewPlatInfo), m.plat_info);
    std::swap(at<void *>(kView3, kViewCamera), m.camera);
    std::swap(at<IDirect3DSurface9 *>(kTarget, kTargetSurface), m.surface);
    swap_lists(&at<ListNode>(kView3, kViewMoverList), &m.movers);
}

// A mover as CDActionDrive makes one for view 3, but with no view (it is in no list the game updates), the player's camera and the shake of the player's view.
void make_mover(Mirror &m, void *anchor, int player) {
    if (!m.camera) m.camera = copy_of(at<void *>(kView3, kViewCamera), kCameraSize);
    if (!m.plat_info) m.plat_info = copy_of(at<void *>(kView3, kViewPlatInfo), kPlatInfoSize);
    void *mover = m.camera && m.plat_info ? reinterpret_cast<void *(__cdecl *)(size_t)>(kNew)(kMoverSize) : nullptr;
    if (!mover) return;
    reinterpret_cast<void *(__fastcall *)(void *, void *, int, int)>(kMoverConstructor)(mover, nullptr, -1, 5);
    *reinterpret_cast<uintptr_t *>(mover) = kMoverVtable;
    at<void *>(mover, kMoverAnchor) = anchor;
    at<void *>(mover, kMoverCamera) = m.camera;
    at<int>(mover, kMoverViewId) = nfsmw::views::kPlayerView[player];
    auto *node = reinterpret_cast<ListNode *>(static_cast<char *>(mover) + 4);  // (a mover's node in a list)
    m.movers.next = m.movers.prev = node;
    node->next = node->prev = &m.movers;
    m.mover = mover;
}

void destroy_mover(Mirror &m) {
    if (m.mover) reinterpret_cast<void(__fastcall *)(void *, void *, int)>((*reinterpret_cast<void ***>(m.mover))[1])(m.mover, nullptr, 1);  // the deleting destructor
    m.mover = nullptr;
    m.shown = false;
}

// The texture of the game's mirror, again for the player: the same size and format.
bool make_texture(IDirect3DDevice9 *device, Mirror &m) {
    if (m.texture) return true;
    auto *game = at<IDirect3DTexture9 *>(kGameTexture, 0);
    D3DSURFACE_DESC d;
    if (!device || !game || FAILED(game->GetLevelDesc(0, &d))) return false;
    if (FAILED(device->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &m.texture, nullptr)) || !m.texture ||
        FAILED(m.texture->GetSurfaceLevel(0, &m.surface))) {
        if (m.texture) m.texture->Release();
        m.texture = nullptr;
        g_log.line("mirrors: no texture of %u x %u (format %d) for a mirror", d.Width, d.Height, static_cast<int>(d.Format));
        return false;
    }
    return true;
}

// Camera Settings' Mirror setting of the camera view when it is loaded, else the game's rule: the Rearview option, and not in the views outside the car (CurCam 2-6).
bool shown_in_view(int cam) {
    if (g_camera_settings_wanted) return g_camera_settings_wanted(cam) != 0;
    void *database = at<void *>(kFEDatabase, 0);
    void *options = database ? at<void *>(database, kOptions) : nullptr;
    return options && at<unsigned char>(options, kRearviewOn) && !(cam >= 2 && cam <= 6);
}

int cur_cam(void *player) {
    void *settings = reinterpret_cast<void *(__fastcall *)(void *, void *)>((*reinterpret_cast<void ***>(player))[kSettingsOfPlayer])(player, nullptr);
    return settings ? at<int>(settings, kCurCam) : -1;
}

bool paused() { return reinterpret_cast<bool(__cdecl *)(bool)>(kShouldPauseSimulation)(true); }

bool looking_back(void *cubic_mover) {
    return cubic_mover && reinterpret_cast<unsigned short(__fastcall *)(void *, void *)>((*reinterpret_cast<void ***>(cubic_mover))[kLookBack])(cubic_mover, nullptr) > 0;
}

// A race is about to load: the anchors the movers hold go with the game's cameras. (Every plugin is loaded by now.)
void on_world_load() {
    for (Mirror &m : g_mirror) destroy_mover(m);
    set_view_on(false);
    g_camera_settings_wanted = nfsmw::camera_settings_api::find_mirror_wanted();
}

void on_frame() {
    if (g_view_on) set_view_on(false);  // (a frame that is not split, the intro, draws no mirror; a game of one player keeps the game's)
    g_view_on = false;
    for (Mirror &m : g_mirror) m.drawn = false;
}

}  // namespace

void update(float dt) {
    const bool racing = g_enabled && players::split_screen();
    const int players = more_players::race_players();
    for (int p = 0; p < nfsmw::kMaxPlayers; ++p) {
        Mirror &m = g_mirror[p];
        void *player = racing && p < players ? more_players::game_player(p) : nullptr;
        void *anchor = nullptr, *cubic = nullptr;
        if (player) extra_cameras::player_camera(p, &anchor, &cubic);
        if (m.mover && at<void *>(m.mover, kMoverAnchor) != anchor) destroy_mover(m);  // (no camera now, or another one)
        if (anchor && !m.mover) make_mover(m, anchor, p);
        m.shown = m.mover && shown_in_view(cur_cam(player)) && !looking_back(cubic);
        if (m.shown) reinterpret_cast<void(__fastcall *)(void *, void *, float)>((*reinterpret_cast<void ***>(m.mover))[2])(m.mover, nullptr, dt);  // Update
    }
}

bool begin_pass(IDirect3DDevice9 *device, int player) {
    set_view_on(false);
    if (!shown(player) || !make_texture(device, g_mirror[player])) return false;
    Mirror &m = g_mirror[player];
    trade(m);
    // The matrices and lighting the game makes for each view it draws. (View 3 keeps its id: the per view setup treats it as the mirror.)
    reinterpret_cast<void(__fastcall *)(void *, void *, void *, int, unsigned, int, int)>(kSetupPerView)(at<void *>(kView3, kViewPlatInfo), nullptr, reinterpret_cast<void *>(kView3), 0,
                                                                                                         kFarPlane, 0, 1);
    reinterpret_cast<void(__fastcall *)(void *, void *)>(kSetupLighting)(reinterpret_cast<void *>(kView3), nullptr);
    set_view_on(true);
    m.drawn = true;
    return true;
}

void end_pass(int player) {
    set_view_on(false);
    trade(g_mirror[player]);
}

void finish_frame() {
    for (const Mirror &m : g_mirror) g_view_on = g_view_on || m.drawn;
    set_view_on(g_view_on);
}

bool shown(int player) { return player >= 0 && player < nfsmw::kMaxPlayers && g_mirror[player].shown && !paused(); }

// The game sets the mirror's texture on the effect it draws the polygon with (kEffectUser) just before; this sets the player's instead.
bool bind(int player) {
    if (player < 0 || player >= nfsmw::kMaxPlayers || !g_mirror[player].drawn) return false;
    void *user = at<void *>(kEffectUser, 0);
    void *effect = user ? at<void *>(user, kEffect) : nullptr;
    void *parameters = user ? at<void *>(user, kEffectParameters) : nullptr;
    if (!effect || !parameters) return false;
    reinterpret_cast<long(__stdcall *)(void *, void *, void *)>((*reinterpret_cast<void ***>(effect))[kEffectSetTexture])(effect, at<void *>(parameters, kTextureHandle),
                                                                                                                        g_mirror[player].texture);
    return true;
}

void release() {
    for (Mirror &m : g_mirror) {
        if (m.surface) m.surface->Release();
        if (m.texture) m.texture->Release();
        m.surface = nullptr;
        m.texture = nullptr;
    }
}

// ---- Options > Advanced Video: Allow Mirror in Split-Screen (the ini's allow_rear_view_mirror, at once)
namespace {

namespace opt = nfsmw::custom_menus;

constexpr unsigned kOnText = 0x417B2604, kOffText = 0x70DFE5C2;  // the game's own "On" and "Off"
constexpr opt::Row kRows[] = {
    {opt::kValueRow, "Allow Mirror in Split-Screen", 0,
     "Gives every player of a split screen race a rear view mirror in the camera views that have one. It costs another picture of the world for each player."},
};
bool g_opened_enabled = true;

void opened() { g_opened_enabled = g_enabled; }
unsigned value(int, char *, unsigned) { return g_enabled ? kOnText : kOffText; }
void change(int, int direction, bool) { g_enabled = direction > 0; }
bool changed() { return g_enabled != g_opened_enabled; }
void accept() {
    WritePrivateProfileStringA("options", "allow_rear_view_mirror", g_enabled ? "1" : "0", g_ini);
    g_log.line("mirrors: %s", g_enabled ? "allowed" : "not allowed");
    g_opened_enabled = g_enabled;
}
void discard() { g_enabled = g_opened_enabled; }
void defaults() { g_enabled = true; }
opt::Page g_page;

}  // namespace

void register_page(const nfsmw::custom_menus::Host *host) {
    g_page = {"split_mirror", "Mirror", "MIRROR", "MIRROR", 0, 0, nullptr, 1, kRows, nullptr, opened, value, change, changed, accept, discard, defaults, opt::kAdvancedVideoOptions, nullptr, nullptr, nullptr};
    host->register_page(&g_page);
}

void install() {
    g_enabled = ini_flag("options", "allow_rear_view_mirror", true);
    events::on_world_load(&on_world_load);
    events::on_frame(&on_frame);
}

}  // namespace split_screen_support::mirrors

/*
 * The PC renderer draws one 3D view. eUpdateViewMode (0x6CFA10) does say "two views" (EVIEWMODE_TWOH, 3) in a split
 * screen race, and player 2's view (eViews[2], 0x9196C0) has its camera (+0x40) kept up to date, but ___eDisplayFrame
 * (0x6DE300), which draws the world with its effects, is written for view 1 (0x919650) only.
 *
 * View 2 is switched on for a split screen race (Active, +8), with view 1's render target (+0x60; the per-view setup
 * reads the target's size, so a view without one would be a null pointer there). The game then does for it what it
 * does for view 1, every frame, before ___eDisplayFrame: the view's matrices (eViewPlatInfo, set up by 0x6CF400), its
 * lighting (eView::SetupWorldLightContext, 0x5029F0) and its place in the track streamer (UpdateCameraMovers, 0x47C880,
 * gives the streamer a position for each active view), so none of that needs doing here.
 * What cannot be done is drawing it: ___eDisplayFrame is hard-wired to view 1. So the frame is drawn twice:
 *
 *   1. ___eDisplayFrame as usual: player 1's view, the whole screen. Its middle band, half the screen's height, is copied
 *      into the top half of a canvas (a render target of the screen's size).
 *   2. The screen and the depth buffer are cleared (the game does that only once per frame, after Present), view 1 and
 *      view 2 trade the fields that make a view what it is (matrices, camera, lighting), and ___eDisplayFrame runs
 *      again. Its band goes into the canvas's bottom half. The views trade back.
 *   3. The canvas is copied onto the back buffer; the HUD and menus are drawn on it after this, as usual.
 *
 * A band of the whole picture is what a view half as high with the same horizontal field of view shows: no distortion,
 * less above and below. The band is centred a little below the middle of the picture (kBandCenter) to keep more of the
 * car in it.
 *
 * Costs: the world is drawn twice per frame. Effects that keep the previous frame (motion blur) could mix the two players'
 * pictures (expected from the code; not seen in game). MaybeChangeViewMode only knows views modes 0-2; the view_mode fix (fixes.cpp) sets view 1 up as for one view
 * when the mode is 3.
 *
 * The canvas lives in video memory; it is released when the game resets the device (a hook on IDirect3DDevice9::Reset,
 * taken from the device's table) and when the split screen game is over. If a copy fails, the split is turned off for
 * the rest of the session (the log says why) and the game draws player 1's view as before.
 */
#include "split_view.h"

#include <d3d9.h>

#include <utility>

#include <MinHook.h>

#include "events.h"
#include "hooks.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::split_view {

namespace {

constexpr uintptr_t kDevice = 0x982BDC;    // IDirect3DDevice9 *
constexpr uintptr_t kViewMode = 0x982B40;  // the current EVIEWMODE
constexpr int kTwoViews = 3;               // EVIEWMODE_TWOH
constexpr uintptr_t kView1 = 0x919650, kView2 = 0x9196C0;
// eView fields (0x70 bytes on PC)
constexpr int kViewPlatInfo = 0x00, kViewActive = 0x08, kViewCamera = 0x40, kViewRenderTarget = 0x60;
constexpr int kViewLighting = 0x5C;  // set by SetupWorldLightContext
// What makes a view what it is for ___eDisplayFrame, as far as it is known: the matrices, the camera, the lighting.
constexpr uintptr_t kFramePool = 0x915F84;  // eFrameMalloc: { current, end }
constexpr int kTraded[] = {kViewPlatInfo, kViewCamera, kViewLighting};

constexpr float kBandCenter = 0.55f;  // where the band is centred, as a fraction of the picture's height

void (*g_orig_display_frame)();

// eView::SetupPerView (0x6CF400): the widescreen fix corrects the projection's aspect for the views with id 1 and 4 only
// (at 0x6CF4EA, which it patches), while the game treats ids 1, 2, 4 and 5 alike there. View 2 is drawn like view 1 here, so
// it gets the id 4, which the fix and the game treat as a main view too, for the call: otherwise player 2's picture is stretched.
// Only when somebody patched that place.
using SetupPerViewFn = void(__fastcall *)(void *, void *, void *, unsigned, unsigned, unsigned, unsigned);
SetupPerViewFn g_orig_setup_per_view;
constexpr uintptr_t kAspectCompare = 0x6CF4EA;  // cmp ecx, 1 (the view's id)
constexpr int kViewId = 0x04;
void __fastcall hook_setup_per_view(void *self, void *edx, void *view, unsigned a, unsigned b, unsigned c, unsigned d) {
    int &id = *reinterpret_cast<int *>(static_cast<char *>(view) + kViewId);
    if (id != 2 || *reinterpret_cast<unsigned char *>(kAspectCompare) == 0x83) return g_orig_setup_per_view(self, edx, view, a, b, c, d);
    id = 4;
    g_orig_setup_per_view(self, edx, view, a, b, c, d);
    id = 2;
}
IDirect3DSurface9 *g_canvas = nullptr;
D3DSURFACE_DESC g_canvas_desc = {};
bool g_failed = false;     // a copy failed: no more splitting this session
bool g_reset_hooked = false;
bool g_logged_first = false;

void release_canvas() {
    if (g_canvas) g_canvas->Release();
    g_canvas = nullptr;
}

template <typename T>
T &at(uintptr_t base, int offset) {
    return *reinterpret_cast<T *>(base + offset);
}

using ResetFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
ResetFn g_orig_reset;
HRESULT WINAPI hook_reset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *params) {
    release_canvas();
    return g_orig_reset(device, params);
}

void hook_device_reset(IDirect3DDevice9 *device) {
    g_reset_hooked = true;
    void *reset = (*reinterpret_cast<void ***>(device))[16];
    MH_STATUS s = MH_CreateHook(reset, reinterpret_cast<void *>(&hook_reset), reinterpret_cast<void **>(&g_orig_reset));
    if (s == MH_OK) s = MH_EnableHook(reset);
    g_log.line("split view: IDirect3DDevice9::Reset at %p %s", reset, s == MH_OK ? "hooked" : MH_StatusToString(s));
}

bool make_canvas(IDirect3DDevice9 *device, const D3DSURFACE_DESC &screen, bool multisampled) {
    release_canvas();
    const HRESULT hr = device->CreateRenderTarget(screen.Width, screen.Height, screen.Format,
                                                  multisampled ? screen.MultiSampleType : D3DMULTISAMPLE_NONE,
                                                  multisampled ? screen.MultiSampleQuality : 0, FALSE, &g_canvas, nullptr);
    if (FAILED(hr)) {
        g_log.line("split view: CreateRenderTarget(%u x %u, format %d) failed: %#lx", screen.Width, screen.Height,
                   static_cast<int>(screen.Format), static_cast<unsigned long>(hr));
        g_canvas = nullptr;
        return false;
    }
    g_canvas->GetDesc(&g_canvas_desc);
    return true;
}

bool canvas_fits(const D3DSURFACE_DESC &screen) {
    return g_canvas && g_canvas_desc.Width == screen.Width && g_canvas_desc.Height == screen.Height &&
           g_canvas_desc.Format == screen.Format;
}

bool copy(IDirect3DDevice9 *device, IDirect3DSurface9 *from, const RECT &from_rect, IDirect3DSurface9 *to,
          const RECT &to_rect, const char *what) {
    const bool stretch = (from_rect.bottom - from_rect.top) != (to_rect.bottom - to_rect.top);
    const HRESULT hr = device->StretchRect(from, &from_rect, to, &to_rect, stretch ? D3DTEXF_LINEAR : D3DTEXF_NONE);
    if (SUCCEEDED(hr)) return true;
    g_log.line("split view: StretchRect (%s) failed: %#lx", what, static_cast<unsigned long>(hr));
    return false;
}

// The game clears the screen and its depth buffer once per frame, after Present (eDisplayFrame: Clear(0, 0, target |
// z | stencil, 0, 1.0, 0)), not before drawing the world. Before player 2's pass the same is done here, on the back
// buffer and view 1's depth surface (its render target +0xC); without it player 2's world is drawn into player 1's
// picture and fails the depth test wherever player 1's was nearer.
void clear_screen(IDirect3DDevice9 *device, IDirect3DSurface9 *back_buffer) {
    const uintptr_t target = at<uintptr_t>(kView1, kViewRenderTarget);
    IDirect3DSurface9 *depth = target ? *reinterpret_cast<IDirect3DSurface9 **>(target + 0xC) : nullptr;
    IDirect3DSurface9 *old_target = nullptr, *old_depth = nullptr;
    D3DVIEWPORT9 old_viewport;
    device->GetRenderTarget(0, &old_target);
    device->GetDepthStencilSurface(&old_depth);
    device->GetViewport(&old_viewport);
    device->SetRenderTarget(0, back_buffer);  // the viewport becomes the whole surface
    if (depth) device->SetDepthStencilSurface(depth);
    HRESULT hr = device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    if (FAILED(hr)) hr = device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);  // no stencil
    static bool logged = false;
    if (!logged) {
        logged = true;
        g_log.line("split view: clearing between the players: depth surface %p, %s", static_cast<void *>(depth),
                   SUCCEEDED(hr) ? "ok" : "failed");
    }
    device->SetRenderTarget(0, old_target ? old_target : back_buffer);
    device->SetDepthStencilSurface(old_depth);
    device->SetViewport(&old_viewport);
    if (old_target) old_target->Release();
    if (old_depth) old_depth->Release();
}

// A view's camera movers are an intrusive circular list whose head node (next, prev) sits in the view (+0x44). The game asks
// the view's first mover which car it follows and whether that car's inside is seen (bumper view: not), so this list is part of
// what makes a view what it is. The two lists trade places node by node: every node points back to its list's head.
struct ListNode {
    ListNode *next, *prev;
};
constexpr int kViewMoverList = 0x44;

void swap_mover_lists() {
    auto *a = reinterpret_cast<ListNode *>(kView1 + kViewMoverList);
    auto *b = reinterpret_cast<ListNode *>(kView2 + kViewMoverList);
    const ListNode from_a = *a, from_b = *b;
    const bool a_empty = from_a.next == a, b_empty = from_b.next == b;
    if (b_empty) a->next = a->prev = a;
    else {
        *a = from_b;
        a->next->prev = a;
        a->prev->next = a;
    }
    if (a_empty) b->next = b->prev = b;
    else {
        *b = from_a;
        b->next->prev = b;
        b->prev->next = b;
    }
}

// The reflection on the cars is a cube map drawn from six extra views (18-23) around the followed car. The game points those
// views at the first player's car once per frame (UpdateEnvironmentMapCameras, 0x756060, only when [0x9017B8] is set), sets
// them up (SetupPerView, 0x6CF400, with the arguments eDisplayFrame gives it) and ___eDisplayFrame then draws them before the
// world. For the second pass the same is done again: the cube map is redrawn around player 2's car.
constexpr uintptr_t kEnvMapEnabled = 0x9017B8, kUpdateEnvMapCameras = 0x756060, kViews = 0x9195E0;
constexpr int kViewSize = 0x70, kFirstEnvMapView = 18, kLastEnvMapView = 23;
constexpr unsigned kFarPlane = 0x466A6000;  // 15000.0f, as eDisplayFrame passes it

// "Car Reflection Update Rate" is [0x9017B8] (1 to 3; 0 = no reflections): eEnvMap::UpdateCameras (0x6DA350) switches on only some of the
// six faces each frame, by the frame number: one face a frame (rate 1, a cycle of 6 frames), three faces (rate 2, 4 frames), all six
// (rate 3). With two players the faces are drawn twice a frame, around player 1 and then around player 2, into one cube map that the
// cars of both passes use: the faces a frame leaves out keep what the other player's pass drew into them, so player 1 saw a reflection
// of player 2's surroundings that came and went. In a split screen race every pass draws all six faces: the rate is 3 for the call.
using UpdateEnvMapCamerasFn = void (*)();
UpdateEnvMapCamerasFn g_orig_update_env_map_cameras = nullptr;
bool g_view2_on = false;  // view 2 is on for a split screen race (switch_view2)

void hook_update_env_map_cameras() {
    int &rate = *reinterpret_cast<int *>(kEnvMapEnabled);
    const int saved = rate;
    if (g_view2_on && saved > 0 && saved < 3) rate = 3;
    g_orig_update_env_map_cameras();
    rate = saved;
}

void update_env_map_for_view1() {
    if (!*reinterpret_cast<int *>(kEnvMapEnabled)) return;
    reinterpret_cast<void(__cdecl *)()>(kUpdateEnvMapCameras)();
    using SetupPerViewFn = void(__fastcall *)(void *plat_info, void *edx, void *view, int unused, unsigned far_plane, int zero, int one);
    const auto setup = reinterpret_cast<SetupPerViewFn>(0x6CF400);
    for (int id = kFirstEnvMapView; id <= kLastEnvMapView; ++id) {
        void *view = reinterpret_cast<void *>(kViews + id * kViewSize);
        setup(*static_cast<void **>(view), nullptr, view, 0, kFarPlane, 0, 1);
    }
}

// The shadow map's view (15) gets its camera and matrices (PlatInfo) only when the shadow map is drawn, which is after the
// scenery for it was picked (eStartFrameCulling, at the top of ___eDisplayFrame). So the picking uses the camera and matrices of
// the previous shadow map drawn: with two passes that is the other player's, and the shadows around each player were chosen by the
// distance to the other one. For the second pass view 15 gets a camera and PlatInfo of its own (copies of its static objects: the
// cameras are 0x2C0 bytes apart, the PlatInfos 0x1A0, GimmeMyViewPlatInfo), so each pass finds its own player's shadow camera from
// the previous frame (the same one frame of lag as in a single player game). View 16 has no camera to trade with: the game
// crashes reading it. Only trading the camera made it worse: the camera and the matrices must stay a pair.
constexpr uintptr_t kShadowView1 = 0x919C70;
constexpr size_t kCameraSize = 0x2C0, kPlatInfoSize = 0x1A0;
void *g_shadow_camera2 = nullptr;
void *g_shadow_plat_info2 = nullptr;

void *copy_of(void *original, size_t size) {
    void *copy = original ? VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
    if (copy) memcpy(copy, original, size);
    return copy;
}

void make_shadow_copies() {
    if ((g_shadow_camera2 && g_shadow_plat_info2)) return;
    g_shadow_camera2 = copy_of(at<void *>(kShadowView1, kViewCamera), kCameraSize);
    g_shadow_plat_info2 = copy_of(at<void *>(kShadowView1, kViewPlatInfo), kPlatInfoSize);
}

// Player 2's view for the world pass, or back.
void trade_views() {
    for (const int offset : kTraded) std::swap(at<void *>(kView1, offset), at<void *>(kView2, offset));
    if (g_shadow_camera2 && g_shadow_plat_info2) {
        std::swap(at<void *>(kShadowView1, kViewCamera), g_shadow_camera2);
        std::swap(at<void *>(kShadowView1, kViewPlatInfo), g_shadow_plat_info2);
    }
    swap_mover_lists();
}

// View 2 on for a split screen race, and off again afterwards (see the top).
void switch_view2(bool on) {
    if (on == g_view2_on) return;
    g_view2_on = on;
    at<void *>(kView2, kViewRenderTarget) = on ? at<void *>(kView1, kViewRenderTarget) : nullptr;
    at<unsigned char>(kView2, kViewActive) = on ? 1 : 0;
    g_log.line("split view: view 2 %s", on ? "on" : "off");
}

void on_world_load() {
    if (players::split_screen()) switch_view2(true);
}

void on_frame() {
    if (g_view2_on && !players::split_screen()) switch_view2(false);
}

void log_first(IDirect3DDevice9 *, IDirect3DSurface9 *back_buffer, const D3DSURFACE_DESC &d) {
    g_logged_first = true;
    const uintptr_t target = at<uintptr_t>(kView1, kViewRenderTarget);
    void *view_surface = target ? *reinterpret_cast<void **>(target + 8) : nullptr;
    g_log.line("split view: first frame: screen %u x %u, format %d, multisampling %d; view 1's surface %s the back buffer",
               d.Width, d.Height, static_cast<int>(d.Format), static_cast<int>(d.MultiSampleType),
               view_surface == back_buffer ? "is" : "is not");
}

void split_frame(IDirect3DDevice9 *device) {
    IDirect3DSurface9 *back_buffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer)) || !back_buffer) return g_orig_display_frame();
    D3DSURFACE_DESC d;
    back_buffer->GetDesc(&d);
    if (!g_logged_first) log_first(device, back_buffer, d);
    if (!canvas_fits(d) && !make_canvas(device, d, d.MultiSampleType != D3DMULTISAMPLE_NONE)) {
        back_buffer->Release();
        g_failed = true;
        return g_orig_display_frame();
    }
    const LONG w = d.Width, h = d.Height, half = h / 2;
    LONG band_top = static_cast<LONG>(kBandCenter * h) - half / 2;
    if (band_top < 0) band_top = 0;
    if (band_top > h - half) band_top = h - half;
    const RECT source = {0, band_top, w, band_top + half};
    const RECT top = {0, 0, w, half}, bottom = {0, half, w, 2 * half};

    make_shadow_copies();
    // The game's per-frame memory (matrices of the flares and headlights, ...): the frame is drawn twice, and the pool is
    // sized for one drawing: the second pass reuses what the first one took, which is dead once its picture is drawn.
    unsigned *pool = reinterpret_cast<unsigned *>(kFramePool);
    const unsigned pool_start = pool[0];
    g_orig_display_frame();  // player 1
    pool[0] = pool_start;
    bool ok = copy(device, back_buffer, source, g_canvas, top, "player 1");
    clear_screen(device, back_buffer);
    trade_views();
    update_env_map_for_view1();  // reflections around player 2's car
    g_orig_display_frame();  // player 2
    trade_views();
    ok = ok && copy(device, back_buffer, source, g_canvas, bottom, "player 2");
    const RECT both = {0, 0, w, 2 * half};
    ok = ok && copy(device, g_canvas, both, back_buffer, both, "to the screen");
    back_buffer->Release();
    if (!ok) {
        g_failed = true;
        release_canvas();
        g_log.line("split view: turned off for this session");
    }
}

void hook_display_frame() {
    auto *device = *reinterpret_cast<IDirect3DDevice9 **>(kDevice);
    const bool two_views = *reinterpret_cast<int *>(kViewMode) == kTwoViews;
    if (!two_views || g_failed || !device || !g_view2_on || !at<void *>(kView1, kViewCamera) ||
        !at<void *>(kView2, kViewCamera)) {
        if (!two_views && g_canvas) release_canvas();
        return g_orig_display_frame();
    }
    if (!g_reset_hooked) hook_device_reset(device);
    split_frame(device);
}

}  // namespace

void install() {
    events::on_world_load(&on_world_load);
    events::on_frame(&on_frame);
    const nfsmw::Hook hooks[] = {
        {"displayframe", "___eDisplayFrame", 0x6DE300, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81}, false,
         nfsmw::detour(&hook_display_frame), nfsmw::orig(&g_orig_display_frame)},
        {"setupperview", "eView::SetupPerView", 0x6CF400, {0x83, 0xEC, 0x0C, 0x53, 0x55, 0x56, 0x57}, false,
         nfsmw::detour(&hook_setup_per_view), nfsmw::orig(&g_orig_setup_per_view)},
        {"envmapcameras", "UpdateEnvironmentMapCameras", kUpdateEnvMapCameras, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83}, false,
         nfsmw::detour(&hook_update_env_map_cameras), nfsmw::orig(&g_orig_update_env_map_cameras)},
    };
    nfsmw::install_hooks(hooks, 3, g_log, nullptr, "split view");
}

}  // namespace split_screen_support::split_view

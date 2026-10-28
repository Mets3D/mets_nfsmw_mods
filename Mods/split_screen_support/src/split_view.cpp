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
#include "view_api.h"

#include <d3d9.h>

#include <algorithm>
#include <utility>

#include <MinHook.h>

#include "events.h"
#include "extra_cameras.h"
#include "fixes.h"
#include "hud_views.h"
#include "layout.h"
#include "max_players.h"
#include "hooks.h"
#include "patch.h"
#include "more_players.h"
#include "players.h"
#include "race_intro.h"
#include "reflections.h"
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

// How many times the game has drawn its frame (the world, for one player) since the frame began (hook_display_frame resets it): the first drawing is player 1's.
int g_draws = 0;
void game_frame() {
    g_orig_display_frame();
    ++g_draws;
}

// VehicleRenderConn::RenderFlares 0x744E20 (static: the view, whether it is a reflection, flags) draws the flares of the cars' lights, and the trails behind them when they use
// nitro, from the last three places the car was drawn at: the car's own history, moved on one place whenever the function runs for the view with the id 1 or 2. With 3 to 8
// players the frame is drawn once per player, each with view 1's id: the history moved 3 to 8 times a frame and its three places were all from the same frame, so the trails
// had no length at all. The drawings after the first leave it alone (the view has an id of nobody's while the function runs; what else the id is for in there is player 1's cop
// light, once per frame).
void (__cdecl *g_orig_render_flares)(void *, int, int);

void __cdecl hook_render_flares(void *view, int reflection, int flags) {
    if (reflection != 0 || g_draws == 0 || !view) {
        g_orig_render_flares(view, reflection, flags);
        return;
    }
    int &id = *reinterpret_cast<int *>(static_cast<char *>(view) + 0x04);
    const int saved = id;
    id = 0x7F;
    g_orig_render_flares(view, reflection, flags);
    id = saved;
}

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

void release_timing();
void release_sun_states();

void release_canvas() {
    if (g_canvas) g_canvas->Release();
    g_canvas = nullptr;
    release_timing();  // queries are lost with the device too
    release_sun_states();
    reflections::release();
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

void swap_mover_lists(uintptr_t other) {
    auto *a = reinterpret_cast<ListNode *>(kView1 + kViewMoverList);
    auto *b = reinterpret_cast<ListNode *>(other + kViewMoverList);
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

bool g_view2_on = false;  // view 2 is on for a split screen race (switch_view2)

// The shadow map's view (15) gets its camera and matrices (PlatInfo) only when the shadow map is drawn, which is after the
// scenery for it was picked (eStartFrameCulling, at the top of ___eDisplayFrame). So the picking uses the camera and matrices of
// the previous shadow map drawn: with two passes that is the other player's, and the shadows around each player were chosen by the
// distance to the other one. For the second pass view 15 gets a camera and PlatInfo of its own (copies of its static objects: the
// cameras are 0x2C0 bytes apart, the PlatInfos 0x1A0, GimmeMyViewPlatInfo), so each pass finds its own player's shadow camera from
// the previous frame (the same one frame of lag as in a single player game). View 16 has no camera to trade with: the game
// crashes reading it. Only trading the camera made it worse: the camera and the matrices must stay a pair.
constexpr uintptr_t kShadowView1 = 0x919C70;
constexpr size_t kCameraSize = 0x2C0, kPlatInfoSize = 0x1A0;
constexpr int kExtraPasses = nfsmw::kMaxPlayers - 1;  // the passes after player 1's
void *g_shadow_camera[kExtraPasses] = {};
void *g_shadow_plat_info[kExtraPasses] = {};

void *copy_of(void *original, size_t size) {
    void *copy = original ? VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
    if (copy) memcpy(copy, original, size);
    return copy;
}

void make_shadow_copies() {
    for (int i = 0; i < kExtraPasses; ++i) {
        if (g_shadow_camera[i] && g_shadow_plat_info[i]) continue;
        g_shadow_camera[i] = copy_of(at<void *>(kShadowView1, kViewCamera), kCameraSize);
        g_shadow_plat_info[i] = copy_of(at<void *>(kShadowView1, kViewPlatInfo), kPlatInfoSize);
    }
}

// The glow view (4): the game draws a second picture of the world's lights (flares, glows) in it, into a target of its own, which the picture is then lit with. It shares
// view 1's camera *object* and has matrices of its own, so with view 1's camera traded for another player's it kept drawing the lights of player 1's view into every other
// player's picture (the headlight and taillight glows of the second player's picture would be seen from the first player's place). It follows view 1's camera, and its
// matrices are made again for the pass (setup_glow_view).
constexpr uintptr_t kGlowView = 0x9197A0;
constexpr unsigned kFarPlane = 0x466A6000;  // 15000.0f, as eDisplayFrame passes it
constexpr uintptr_t kSetupPerView = 0x6CF400;

void follow_with_glow_view() { at<void *>(kGlowView, kViewCamera) = at<void *>(kView1, kViewCamera); }

void setup_glow_view() {
    follow_with_glow_view();
    using SetupPerViewFn = void(__fastcall *)(void *plat_info, void *edx, void *view, int unused, unsigned far_plane, int zero, int one);
    reinterpret_cast<SetupPerViewFn>(kSetupPerView)(at<void *>(kGlowView, kViewPlatInfo), nullptr, reinterpret_cast<void *>(kGlowView), 0, kFarPlane, 0, 1);
}

// The sun (sub_6D04E0, called by each pass of ___eDisplayFrame with the object at 0x93E018) is shown as bright as the part of a small quad at its place that is not behind
// anything, which is counted by four D3D occlusion queries used in turn (the result of the query issued three calls ago, smoothed into the intensity at +0x18).
// Every pass of a split screen frame is a call, so a pass read the result of the pass before it: the sun was as bright for player 2 as it was for player 1, and
// drawn (where the view's matrices put it, mirrored when it is behind the camera) when only player 1 could see it. Each pass after the first has a copy of the
// object with queries of its own, swapped in with the views (trade_views).
constexpr uintptr_t kSunOcclusion = 0x93E018;
constexpr size_t kSunStateSize = 0x58;
constexpr int kSunQueriesAt = 0x24, kSunQueries = 4, kSunReady = 0x10, kSunNext = 0x14, kSunIntensity = 0x18;
struct SunState {
    unsigned char bytes[kSunStateSize];
    bool made = false;
};
SunState g_sun_state[nfsmw::kMaxPlayers];
IDirect3DDevice9 *g_device = nullptr;

IDirect3DQuery9 *&sun_query(unsigned char *state, int i) { return *reinterpret_cast<IDirect3DQuery9 **>(state + kSunQueriesAt + 4 * i); }

void release_sun_states() {
    for (SunState &state : g_sun_state) {
        if (!state.made) continue;
        for (int i = 0; i < kSunQueries; ++i)
            if (sun_query(state.bytes, i)) sun_query(state.bytes, i)->Release();
        state.made = false;
    }
}

void trade_sun_state(int pass) {
    unsigned char *game = reinterpret_cast<unsigned char *>(kSunOcclusion);
    SunState &state = g_sun_state[pass];
    if (!state.made) {
        if (!g_device || !sun_query(game, 0)) return;  // (the game has no queries: it does not look)
        memcpy(state.bytes, game, kSunStateSize);
        state.bytes[kSunReady] = 0;
        *reinterpret_cast<int *>(state.bytes + kSunNext) = 0;
        *reinterpret_cast<float *>(state.bytes + kSunIntensity) = 1.0f;
        for (int i = 0; i < kSunQueries; ++i) {
            if (SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &sun_query(state.bytes, i)))) continue;
            sun_query(state.bytes, i) = nullptr;
            state.made = true;  // releases the ones made
            release_sun_states();
            return;
        }
        state.made = true;
    }
    for (size_t i = 0; i < kSunStateSize; ++i) std::swap(game[i], state.bytes[i]);
}

// Another player's view for the world pass, or back: view 1 and `other` (view 2 for player 2, the views of extra_cameras.h for players 3 and 4) trade what makes
// a view what it is. `pass` is the pass (1: player 2, 2: player 3, 3: player 4), which has a shadow camera of its own.
void trade_views(int pass, uintptr_t other) {
    for (const int offset : kTraded) std::swap(at<void *>(kView1, offset), at<void *>(other, offset));
    if (g_shadow_camera[pass - 1] && g_shadow_plat_info[pass - 1]) {
        std::swap(at<void *>(kShadowView1, kViewCamera), g_shadow_camera[pass - 1]);
        std::swap(at<void *>(kShadowView1, kViewPlatInfo), g_shadow_plat_info[pass - 1]);
    }
    swap_mover_lists(other);
    follow_with_glow_view();
    trade_sun_state(pass);
}

// View 2 on for a split screen race, and off again afterwards (see the top).
void switch_view2(bool on) {
    if (on == g_view2_on) return;
    g_view2_on = on;
    reflections::set_split(on);
    at<void *>(kView2, kViewRenderTarget) = on ? at<void *>(kView1, kViewRenderTarget) : nullptr;
    at<unsigned char>(kView2, kViewActive) = on ? 1 : 0;
    g_log.line("split view: view 2 %s", on ? "on" : "off");
}

void on_world_load() {
    if (players::split_screen()) switch_view2(true);
    fixes::use_one_view_scenery_rule(more_players::extras_in_race() == 0);
    hud_views::clear();
}

void on_frame() {
    if (g_view2_on && !players::split_screen()) {
        switch_view2(false);
        hud_views::clear();
    }
}

void log_first(IDirect3DDevice9 *, IDirect3DSurface9 *back_buffer, const D3DSURFACE_DESC &d) {
    g_logged_first = true;
    const uintptr_t target = at<uintptr_t>(kView1, kViewRenderTarget);
    void *view_surface = target ? *reinterpret_cast<void **>(target + 8) : nullptr;
    g_log.line("split view: first frame: screen %u x %u, format %d, multisampling %d; view 1's surface %s the back buffer",
               d.Width, d.Height, static_cast<int>(d.Format), static_cast<int>(d.MultiSampleType),
               view_surface == back_buffer ? "is" : "is not");
}

// ---- Where the time of a four player frame goes ([options] frame_timing=1 in the ini): the GPU time of each pass (a timestamp query before the first
// and after each pass, read two frames later so the reading does not wait) and the CPU time the game spends in each pass call. Every 300 frames the
// averages go to the log.
constexpr int kTimedFrames = 3, kTimestamps = nfsmw::kMaxPlayers + 1;  // a timestamp before the first pass and after each
IDirect3DQuery9 *g_timestamp[kTimedFrames][kTimestamps] = {};
IDirect3DQuery9 *g_frequency_query = nullptr;
unsigned long long g_frequency = 0;
unsigned g_timed_frame = 0, g_timing_samples = 0;
double g_gpu_ms[kTimestamps - 1] = {}, g_cpu_ms[kTimestamps - 1] = {};
int g_timed_passes = 2;  // the passes of the frame being timed
bool g_timing_on = false, g_timing_failed = false;

double now_ms() {
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return counter.QuadPart * 1000.0 / frequency.QuadPart;
}

void release_timing() {
    for (auto &frame : g_timestamp)
        for (auto &query : frame) {
            if (query) query->Release();
            query = nullptr;
        }
    if (g_frequency_query) g_frequency_query->Release();
    g_frequency_query = nullptr;
    g_frequency = 0;
}

bool start_timing(IDirect3DDevice9 *device) {
    if (g_timing_failed) return false;
    if (!g_frequency_query) {
        if (FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &g_frequency_query))) {
            g_timing_failed = true;
            g_log.line("frame timing: the GPU has no timestamp queries");
            return false;
        }
        g_frequency_query->Issue(D3DISSUE_END);
    }
    if (!g_frequency && g_frequency_query->GetData(&g_frequency, sizeof(g_frequency), D3DGETDATA_FLUSH) != S_OK) g_frequency = 0;
    for (auto &query : g_timestamp[g_timed_frame % kTimedFrames]) {
        if (!query && FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &query))) {
            g_timing_failed = true;
            return false;
        }
    }
    return true;
}

// The timestamps of the frame two frames back, when all are there.
void read_timing() {
    if (g_timed_frame < 2 || !g_frequency) return;
    IDirect3DQuery9 **old = g_timestamp[(g_timed_frame - 2) % kTimedFrames];
    unsigned long long value[kTimestamps];
    for (int i = 0; i < kTimestamps; ++i)
        if (!old[i] || old[i]->GetData(&value[i], sizeof(value[i]), 0) != S_OK) return;
    for (int pass = 0; pass < kTimestamps - 1; ++pass) g_gpu_ms[pass] += (value[pass + 1] - value[pass]) * 1000.0 / g_frequency;
    if (++g_timing_samples < 300) return;
    char text[512];
    int length = snprintf(text, sizeof(text), "frame timing, per pass (GPU ms / CPU ms, average of %u frames):", g_timing_samples);
    double gpu_total = 0;
    for (int pass = 0; pass < g_timed_passes && length < static_cast<int>(sizeof(text)); ++pass) {
        length += snprintf(text + length, sizeof(text) - length, "%s player %d %.2f / %.2f", pass ? "," : "", pass + 1, g_gpu_ms[pass] / g_timing_samples, g_cpu_ms[pass] / g_timing_samples);
        gpu_total += g_gpu_ms[pass];
    }
    g_log.line("%s; the GPU time of all %.2f ms", text, gpu_total / g_timing_samples);
    g_timing_samples = 0;
    for (int i = 0; i < kTimestamps - 1; ++i) g_gpu_ms[i] = g_cpu_ms[i] = 0;
}

// Three or four players: every pass is the whole picture, shrunk into its cell of the canvas (a quadrant has the screen's aspect: nothing is cropped), except
// the first player's with three players, who has the top half: a band of the picture, as with two. The cells are those of layout.h: with four players
// player 1 is the top left, player 2 the top right, players 3 and 4 the bottom row; with three player 1 has the top half and players 2 and 3 the bottom
// row. The HUD and menus are drawn over this by the game as if for the screen in two halves.
bool multi_view_frame(IDirect3DDevice9 *device, IDirect3DSurface9 *back_buffer, const D3DSURFACE_DESC &d, int players) {
    const LONG w = d.Width, h = d.Height, qw = w / 2, qh = h / 2;
    const RECT full = {0, 0, w, h};
    RECT cells[nfsmw::kMaxPlayers];
    for (int i = 0; i < players; ++i) cells[i] = layout::cell(i, players, 2 * qw, 2 * qh);
    RECT first_source = full;  // what of player 1's picture goes to the first cell
    if (players == 3) {
        LONG band_top = std::clamp(static_cast<LONG>(kBandCenter * h) - qh / 2, 0L, h - qh);
        first_source = {0, band_top, w, band_top + qh};
    }
    char names[nfsmw::kMaxPlayers][16];
    for (int i = 0; i < players; ++i) snprintf(names[i], sizeof(names[i]), "player %d", i + 1);
    unsigned *pool = reinterpret_cast<unsigned *>(kFramePool);
    const unsigned pool_start = pool[0];
    const bool timed = g_timing_on && start_timing(device);
    g_timed_passes = players;
    IDirect3DQuery9 **stamps = g_timestamp[g_timed_frame % kTimedFrames];
    double cpu_start = 0;
    if (timed) {
        read_timing();
        stamps[0]->Issue(D3DISSUE_END);
        cpu_start = now_ms();
    }
    reflections::begin_pass(device, 0, players);
    game_frame();  // player 1
    pool[0] = pool_start;
    reflections::end_pass(device, 0);
    if (timed) {
        g_cpu_ms[0] += now_ms() - cpu_start;
        stamps[1]->Issue(D3DISSUE_END);
    }
    view_api::draw(device, at<void *>(kView1, kViewPlatInfo), static_cast<float>(w), static_cast<float>(h), 0);
    bool ok = copy(device, back_buffer, first_source, g_canvas, cells[0], names[0]);
    for (int pass = 1; pass < players && ok; ++pass) {
        const bool extra = pass >= 2;
        const uintptr_t other = extra ? reinterpret_cast<uintptr_t>(extra_cameras::prepare_pass(pass - 2)) : kView2;
        if (!other) continue;  // no camera yet: the quadrant keeps what it had
        clear_screen(device, back_buffer);
        trade_views(pass, other);
        setup_glow_view();
        if (timed) cpu_start = now_ms();
        reflections::begin_pass(device, pass, players);
        game_frame();
        pool[0] = pool_start;
        reflections::end_pass(device, pass);
        if (timed) {
            g_cpu_ms[pass] += now_ms() - cpu_start;
            stamps[pass + 1]->Issue(D3DISSUE_END);
        }
        view_api::draw(device, at<void *>(kView1, kViewPlatInfo), static_cast<float>(w), static_cast<float>(h), pass);  // with the player's view in view 1's place
        trade_views(pass, other);
        ok = copy(device, back_buffer, full, g_canvas, cells[pass], names[pass]);
    }
    if (timed) {
        for (int i = players + 1; i < kTimestamps; ++i) stamps[i]->Issue(D3DISSUE_END);  // the passes there are not: no time
        ++g_timed_frame;
    }
    const RECT canvas_part = {0, 0, 2 * qw, 2 * qh};
    return ok && copy(device, g_canvas, canvas_part, back_buffer, canvas_part, "to the screen");
}

void split_frame(IDirect3DDevice9 *device) {
    g_device = device;
    IDirect3DSurface9 *back_buffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer)) || !back_buffer) return game_frame();
    D3DSURFACE_DESC d;
    back_buffer->GetDesc(&d);
    if (!g_logged_first) log_first(device, back_buffer, d);
    if (!canvas_fits(d) && !make_canvas(device, d, d.MultiSampleType != D3DMULTISAMPLE_NONE)) {
        back_buffer->Release();
        g_failed = true;
        return game_frame();
    }
    const LONG w = d.Width, h = d.Height, half = h / 2;
    LONG band_top = static_cast<LONG>(kBandCenter * h) - half / 2;
    if (band_top < 0) band_top = 0;
    if (band_top > h - half) band_top = h - half;
    const RECT source = {0, band_top, w, band_top + half};
    const RECT top = {0, 0, w, half}, bottom = {0, half, w, 2 * half};

    make_shadow_copies();
    if (more_players::extra_player_count() > 0) {
        const bool ok = multi_view_frame(device, back_buffer, d, nfsmw::kPlayersPerRound + more_players::extra_player_count());
        back_buffer->Release();
        if (!ok) {
            g_failed = true;
            release_canvas();
            g_log.line("split view: turned off for this session");
        }
        return;
    }
    // The game's per-frame memory (matrices of the flares and headlights, ...): the frame is drawn twice, and the pool is
    // sized for one drawing: the second pass reuses what the first one took, which is dead once its picture is drawn.
    unsigned *pool = reinterpret_cast<unsigned *>(kFramePool);
    const unsigned pool_start = pool[0];
    reflections::begin_pass(device, 0, 2);
    game_frame();  // player 1
    pool[0] = pool_start;
    reflections::end_pass(device, 0);
    view_api::draw(device, at<void *>(kView1, kViewPlatInfo), static_cast<float>(d.Width), static_cast<float>(d.Height), 0);
    bool ok = copy(device, back_buffer, source, g_canvas, top, "player 1");
    clear_screen(device, back_buffer);
    trade_views(1, kView2);
    setup_glow_view();
    reflections::begin_pass(device, 1, 2);  // reflections around player 2's car
    game_frame();  // player 2
    reflections::end_pass(device, 1);
    view_api::draw(device, at<void *>(kView1, kViewPlatInfo), static_cast<float>(d.Width), static_cast<float>(d.Height), 1);  // with player 2's view still in view 1's place
    trade_views(1, kView2);
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

// The one picture of a game that is not split: what other mods asked to draw (view_api.h), as player 0.
void draw_single_view(IDirect3DDevice9 *device) {
    if (!device || !view_api::wanted()) return;
    IDirect3DSurface9 *back_buffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer)) || !back_buffer) return;
    D3DSURFACE_DESC d;
    back_buffer->GetDesc(&d);
    back_buffer->Release();
    view_api::draw(device, at<void *>(kView1, kViewPlatInfo), static_cast<float>(d.Width), static_cast<float>(d.Height), 0);
}

void hook_display_frame() {
    g_draws = 0;
    auto *device = *reinterpret_cast<IDirect3DDevice9 **>(kDevice);
    const bool two_views = *reinterpret_cast<int *>(kViewMode) == kTwoViews && !race_intro::playing();  // (the cutscene of the race is for the whole screen)
    if (!two_views || g_failed || !device || !g_view2_on || !at<void *>(kView1, kViewCamera) ||
        !at<void *>(kView2, kViewCamera)) {
        if (!two_views && g_canvas && !race_intro::playing()) release_canvas();
        game_frame();
        draw_single_view(device);
        return;
    }
    if (!g_reset_hooked) hook_device_reset(device);
    split_frame(device);
}

}  // namespace

void install() {
    g_timing_on = ini_flag("options", "frame_timing", false);
    events::on_world_load(&on_world_load);
    events::on_frame(&on_frame);
    const nfsmw::Hook hooks[] = {
        {"displayframe", "___eDisplayFrame", 0x6DE300, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81}, false,
         nfsmw::detour(&hook_display_frame), nfsmw::orig(&g_orig_display_frame)},
        {"renderflares", "VehicleRenderConn::RenderFlares", 0x744E20, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83}, false, nfsmw::detour(&hook_render_flares),
         nfsmw::orig(&g_orig_render_flares)},
        {"setupperview", "eView::SetupPerView", 0x6CF400, {0x83, 0xEC, 0x0C, 0x53, 0x55, 0x56, 0x57}, false,
         nfsmw::detour(&hook_setup_per_view), nfsmw::orig(&g_orig_setup_per_view)},
    };
    nfsmw::install_hooks(hooks, 3, g_log, nullptr, "split view");
    reflections::install();
}

}  // namespace split_screen_support::split_view

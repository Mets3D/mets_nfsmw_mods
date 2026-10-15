// The world map screen (WorldMap, docs/menus-and-camera.md) is driven by the mouse on PC. The console cursor is still in the
// exe: WorldMap::UpdateCursor(bool) 0x552F40 turns the velocity at +0x128 / +0x12C into cursor movement (panning the map at its
// edges, snapping to icons), but nothing calls it and nothing writes the velocity. The mouse code, UpdateMouseCursor 0x552DA0,
// runs once per screen tick; while a controller is in use it is replaced by the console path, with the right stick as the
// velocity. LT and RT zoom continuously (the zoom and pan are set directly, so the game's zoom animation, during which it freezes the
// cursor, never runs); LB and RB still step through the zoom levels as in the vanilla game.
#include "world_map.h"

#include "hooks.h"
#include "input.h"

namespace pad::world_map {

namespace {

using UpdateFn = void(FASTCALL *)(void *, void *);
using UpdateCursorFn = void(__attribute__((thiscall)) *)(void *, bool);
using StreamerFn = void(__attribute__((thiscall)) *)(void *);           // UpdateAnimation, WorldMap::RefreshHeader
using StreamerFactorFn = float(__attribute__((thiscall)) *)(void *);     // GetZoomFactor (1 / the visible fraction of the map)
using StreamerVec2Fn = void(__attribute__((thiscall)) *)(void *, float *);  // GetPan (centred on 0), SetPan / SetZoom (0..1, snap)
using GetCenterFn = void(__attribute__((cdecl)) *)(void *, float *, float *);  // FEngGetCenter
using SetVisibleFn = void(__attribute__((cdecl)) *)(void *);

UpdateFn g_orig_update = nullptr;
using MoveCursorFn = void(FASTCALL *)(void *, void *, float, float);  // WorldMap::MoveCursor 0x530660 (this, x, y), ret 8
MoveCursorFn g_orig_move_cursor = nullptr;

constexpr uintptr_t kUpdateCursor = 0x552F40, kSetVisible = 0x514CC0, kGetCenter = 0x524EE0, kRefreshHeader = 0x5488B0;
constexpr uintptr_t kUpdateAnimation = 0x510C70, kGetZoomFactor = 0x510CA0, kGetPan = 0x510CE0, kSetZoom = 0x510E20, kSetPan = 0x510E60;
constexpr uintptr_t kMouseX = 0x987F54, kMouseY = 0x987F58;  // ints
// WorldMap members: the cursor object, the velocity (screen units per tick), in toggle mode, the current zoom level.
constexpr unsigned kCursor = 0x124, kVelocity = 0x128, kToggleMode = 0x170, kZoomLevel = 0x168, kStreamer = 0x160, kTrackMap = 0x140, kMapSize = 0x14C;
constexpr float kZoomLevels[] = {1.0f, 2.0f, 3.3f, 5.0f};  // WorldMap::GetZoomFactor
constexpr float kMinFactor = 1.0f, kMaxFactor = 5.0f;
constexpr float kZoomRate = 0.7f;  // e-folds per second with a trigger fully down (1 to 5 takes about 2.3 s)
constexpr float kStickSpeed = 420.0f;  // screen units per second at full deflection (the console moved 14 per tick, 30 ticks a second)
constexpr float kStickDeadzone = 0.25f;
constexpr BYTE kTriggerDeadzone = 30;

bool g_pad_mode = false;  // the controller moves the cursor (until the mouse does)
int g_mouse_x = 0, g_mouse_y = 0;  // the pointer when the controller took over
DWORD g_last_tick = 0;

float axis(SHORT v) {
    const float f = v / 32767.0f;
    const float m = std::fabs(f);
    if (m < kStickDeadzone) return 0.0f;
    return (f < 0 ? -1.0f : 1.0f) * (m - kStickDeadzone) / (1.0f - kStickDeadzone);
}

float trigger(BYTE v) { return v <= kTriggerDeadzone ? 0.0f : (v - kTriggerDeadzone) / (255.0f - kTriggerDeadzone); }

// Zooms by `amount` (positive in) around the cursor: the point of the map under the cursor stays under it, as far as the map's
// edges allow. The map shows a square window of the picture, `pan` (0..1) its centre and 1 / factor its size.
void zoom_by(void *self, float amount, float dt) {
    void *streamer = field<void *>(self, kStreamer);
    if (!streamer) return;
    const float factor = reinterpret_cast<StreamerFactorFn>(kGetZoomFactor)(streamer);
    const float next = std::fmin(kMaxFactor, std::fmax(kMinFactor, factor * std::exp(amount * kZoomRate * dt)));
    if (next == factor) return;
    const float size = 1.0f / factor, next_size = 1.0f / next;

    float pan[2] = {0, 0}, cursor[2] = {0, 0}, centre[2] = {0, 0};
    reinterpret_cast<StreamerVec2Fn>(kGetPan)(streamer, pan);
    pan[0] += 0.5f;
    pan[1] += 0.5f;
    reinterpret_cast<GetCenterFn>(kGetCenter)(field<void *>(self, kCursor), &cursor[0], &cursor[1]);
    reinterpret_cast<GetCenterFn>(kGetCenter)(field<void *>(self, kTrackMap), &centre[0], &centre[1]);
    const float off_x = (cursor[0] - centre[0]) / field<float>(self, kMapSize);
    const float off_y = (cursor[1] - centre[1]) / field<float>(self, kMapSize + 4);
    const float half = next_size * 0.5f;
    float next_pan[2] = {pan[0] + off_x * (size - next_size), pan[1] + off_y * (size - next_size)};
    for (float &v : next_pan) v = std::fmin(1.0f - half, std::fmax(half, v));

    float zoom[2] = {next_size, next_size};
    reinterpret_cast<StreamerVec2Fn>(kSetZoom)(streamer, zoom);
    reinterpret_cast<StreamerVec2Fn>(kSetPan)(streamer, next_pan);
    reinterpret_cast<StreamerFn>(kUpdateAnimation)(streamer);  // the picture follows now, not on the next tick

    // The zoom level LB and RB step from, and the header that shows it: the nearest level.
    int nearest = 0;
    for (int i = 1; i < 4; ++i)
        if (std::fabs(std::log(next / kZoomLevels[i])) < std::fabs(std::log(next / kZoomLevels[nearest]))) nearest = i;
    int &level = field<int>(self, kZoomLevel);
    if (level != nearest) {
        level = nearest;
        reinterpret_cast<StreamerFn>(kRefreshHeader)(self);
    }
}

// MoveCursor pans the map when the cursor is past an edge, but only for one edge at a time (an else-if chain: left, right,
// top, bottom), so pushing into a corner panned along one axis only. One call per axis pans both.
void FASTCALL hook_move_cursor(void *self, void *edx, float x, float y) {
    if (x != 0.0f) g_orig_move_cursor(self, edx, x, 0.0f);
    if (y != 0.0f) g_orig_move_cursor(self, edx, 0.0f, y);
}

void FASTCALL hook_update(void *self, void *edx) {
    const int mx = *reinterpret_cast<int *>(kMouseX), my = *reinterpret_cast<int *>(kMouseY);
    xinput::Gamepad p = {};
    const bool have_pad = input::read_merged(&p);
    const float sx = have_pad ? axis(p.rx) : 0.0f, sy = have_pad ? axis(p.ry) : 0.0f;
    const bool zoom_out = have_pad && p.left_trigger > kTriggerDeadzone, zoom_in = have_pad && p.right_trigger > kTriggerDeadzone;

    if (sx != 0.0f || sy != 0.0f || zoom_out || zoom_in) {
        if (!g_pad_mode) {
            g_pad_mode = true;
            g_mouse_x = mx;
            g_mouse_y = my;
        }
    } else if (g_pad_mode && (mx != g_mouse_x || my != g_mouse_y)) {
        g_pad_mode = false;  // the mouse moved
    }
    if (!g_pad_mode) {
        g_orig_update(self, edx);
        return;
    }

    const DWORD now = GetTickCount();
    float dt = (now - g_last_tick) / 1000.0f;
    if (g_last_tick == 0 || dt > 0.1f) dt = 0.033f;
    g_last_tick = now;

    // Not while a vanilla zoom step (LB / RB) is still animating: the streamer's 16-bit counts at +0x44 and +0x70.
    const unsigned char *streamer = field<unsigned char *>(self, kStreamer);
    const bool animating = streamer && (*reinterpret_cast<const unsigned short *>(streamer + 0x44) ||
                                        *reinterpret_cast<const unsigned short *>(streamer + 0x70));
    if ((zoom_out || zoom_in) && !animating && !field<bool>(self, kToggleMode)) zoom_by(self, trigger(p.right_trigger) - trigger(p.left_trigger), dt);

    float *velocity = &field<float>(self, kVelocity);
    velocity[0] = sx * kStickSpeed * dt;
    velocity[1] = -sy * kStickSpeed * dt;  // the stick's up is positive, the screen's y grows downwards
    reinterpret_cast<SetVisibleFn>(kSetVisible)(field<void *>(self, kCursor));
    reinterpret_cast<UpdateCursorFn>(kUpdateCursor)(self, false);
}

}  // namespace

void install_hooks() {
    const nfsmw::Hook hooks[] = {
        {"world_map", "WorldMap::UpdateMouseCursor", 0x552DA0, {0x83, 0xEC, 0x18, 0x56, 0x8B, 0xF1, 0x8A}, false,
         nfsmw::detour(&hook_update), nfsmw::orig(&g_orig_update)},
        {"world_map_move", "WorldMap::MoveCursor", 0x530660, {0x83, 0xEC, 0x30, 0x57, 0x8B, 0xF9, 0x8B}, false,
         nfsmw::detour(&hook_move_cursor), nfsmw::orig(&g_orig_move_cursor)},
    };
    nfsmw::install_hooks(hooks, 2, g_log, nullptr, "world map");
}

}  // namespace pad::world_map

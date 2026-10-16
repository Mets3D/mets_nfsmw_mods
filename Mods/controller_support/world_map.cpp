// The world map screen (WorldMap, docs/menus-and-camera.md) is driven by the mouse on PC. The console cursor is still in the
// exe: WorldMap::UpdateCursor(bool) 0x552F40 turns the velocity at +0x128 / +0x12C into cursor movement (panning the map at its
// edges, snapping to icons), but nothing calls it and nothing writes the velocity. The mouse code, UpdateMouseCursor 0x552DA0,
// runs once per screen tick; while a controller is in use it is replaced by the console path, with the right stick as the
// velocity. LT and RT zoom continuously (the zoom and pan are set directly, so the game's zoom animation, during which it freezes the
// cursor, never runs); LB and RB zoom the same way, like a trigger held fully down (the game's own stepped zoom is swallowed).
// SnapCursor, which locks the cursor onto the markers (races, shops, garages), is replaced by a version that measures from the cursor.
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
using SnapCursorFn = bool(FASTCALL *)(void *, void *);  // WorldMap::SnapCursor 0x530A60 (this), returns whether the selection changed
SnapCursorFn g_orig_snap_cursor = nullptr;
using ScrollZoomFn = void(FASTCALL *)(void *, void *, int);  // WorldMap::ScrollZoom 0x552CB0 (this, direction -1 / 1), ret 4
ScrollZoomFn g_orig_scroll_zoom = nullptr;

constexpr uintptr_t kUpdateCursor = 0x552F40, kSetVisible = 0x514CC0, kGetCenter = 0x524EE0, kRefreshHeader = 0x5488B0;
constexpr uintptr_t kUpdateAnimation = 0x510C70, kGetZoomFactor = 0x510CA0, kGetPan = 0x510CE0, kSetZoom = 0x510E20, kSetPan = 0x510E60;
constexpr uintptr_t kMouseX = 0x987F54, kMouseY = 0x987F58;  // ints
// WorldMap members: the cursor object, the velocity (screen units per tick), in toggle mode, the current zoom level, the marker the
// cursor is locked onto.
constexpr unsigned kCursor = 0x124, kVelocity = 0x128, kToggleMode = 0x170, kZoomLevel = 0x168, kSelectedItem = 0x15C, kStreamer = 0x160, kTrackMap = 0x140, kMapSize = 0x14C;
constexpr float kZoomLevels[] = {1.0f, 2.0f, 3.3f, 5.0f};  // WorldMap::GetZoomFactor
constexpr float kMinFactor = 1.0f, kMaxFactor = 5.0f;
constexpr float kZoomRate = 0.7f;  // e-folds per second with a trigger fully down (1 to 5 takes about 2.3 s)
constexpr float kStickSpeed = 420.0f;  // screen units per second at full deflection (the console moved 14 per tick, 30 ticks a second)
constexpr float kStickDeadzone = 0.25f;
constexpr BYTE kTriggerDeadzone = 30;
constexpr uintptr_t kFindScript = 0x5B1360, kSetScript = 0x5B8400, kSetCenter = 0x525050;
constexpr unsigned kSnapScript = 0x1CBF71, kUnsnapScript = 0x7EFE8FF4;  // the cursor's scripts for locking onto / leaving a marker
constexpr unsigned kSnapDistance = 0x174;  // WorldMap member: float, how close the cursor must be to a marker (20)
using FindScriptFn = void *(FASTCALL *)(void *, void *, unsigned);  // FEObject::FindScript (this, hash)
using SetScriptFn = void(__attribute__((thiscall)) *)(void *, void *, bool);  // FEObject::SetScript (this, script, bool)
using SetCenterFn = void(__attribute__((cdecl)) *)(void *, float, float);  // FEngSetCenter

// ScrollZoom is called with the pad's LB / RB message (from these two sites) and with the mouse wheel (the other two).
constexpr uintptr_t kPadZoomOutCall = 0x55B17E + 5, kPadZoomInCall = 0x55B6D3 + 5;
int g_shoulder_dir = 0;  // -1 / 1: a zoom message arrived from LB / RB (it counts as held for kShoulderHold, as a tap can be shorter than a tick)
DWORD g_shoulder_until = 0;
constexpr DWORD kShoulderHold = 120;

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

// LB and RB step through the zoom levels in the vanilla game, which looks worse than the continuous zoom of the triggers: their
// message is swallowed and they zoom like a trigger held fully down (hook_update).
void FASTCALL hook_scroll_zoom(void *self, void *edx, int dir) {
    const uintptr_t from = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    if (from == kPadZoomOutCall || from == kPadZoomInCall) {
        g_shoulder_dir = dir;
        g_shoulder_until = GetTickCount() + kShoulderHold;
        return;
    }
    g_orig_scroll_zoom(self, edx, dir);
}

// The map items (markers): a circular list at +0x154, the item's link at +4, its FEObject at +0xC, hidden at +0x34.
template <typename F> void for_each_item(void *self, F visit) {
    unsigned char *const end = reinterpret_cast<unsigned char *>(self) + 0x154 - 4;
    unsigned char *node = field<unsigned char *>(self, 0x154);
    int guard = 0;
    for (unsigned char *item = node ? node - 4 : nullptr; item && item != end && guard++ < 1000;) {
        visit(item);
        unsigned char *next = *reinterpret_cast<unsigned char **>(item + 4);
        item = next ? next - 4 : nullptr;
    }
}

// Plays a script of the cursor object from its start (the snapped / unsnapped look).
void play_cursor_script(void *cursor, unsigned hash) {
    if (!cursor) return;
    void *script = reinterpret_cast<FindScriptFn>(kFindScript)(cursor, nullptr, hash);
    if (!script) return;
    reinterpret_cast<SetScriptFn>(kSetScript)(cursor, script, false);
    field<int>(script, 0x10) = 0;
}

// WorldMap::SnapCursor 0x530A60 measures from the mouse pointer (the console version from the cursor), which the controller leaves
// where it was: the cursor snapped to whatever was near the stale pointer. This is the same, measured from the cursor.
bool FASTCALL hook_snap_cursor(void *self, void *edx) {
    if (!g_pad_mode) return g_orig_snap_cursor(self, edx);
    void *const cursor_obj = field<void *>(self, kCursor);
    float cursor[2] = {0, 0};
    reinterpret_cast<GetCenterFn>(kGetCenter)(cursor_obj, &cursor[0], &cursor[1]);
    const float snap_distance = field<float>(self, kSnapDistance);
    unsigned char *snap_to = nullptr;
    float best = 1e8f, target[2] = {0, 0};
    for_each_item(self, [&](unsigned char *item) {
        if (item[0x34]) return;  // hidden
        float pos[2] = {0, 0};
        reinterpret_cast<GetCenterFn>(kGetCenter)(*reinterpret_cast<void **>(item + 0xC), &pos[0], &pos[1]);
        const float d = std::hypot(pos[0] - cursor[0], pos[1] - cursor[1]);
        if (d < snap_distance && d < best) {
            best = d;
            snap_to = item;
            target[0] = pos[0];
            target[1] = pos[1];
        }
    });
    void *&selected = field<void *>(self, kSelectedItem);
    if (snap_to) {
        reinterpret_cast<SetCenterFn>(kSetCenter)(cursor_obj, target[0], target[1]);
        if (snap_to == selected) return false;
        selected = snap_to;
        play_cursor_script(cursor_obj, kSnapScript);
        return true;
    }
    if (!selected) return false;
    play_cursor_script(cursor_obj, kUnsnapScript);
    selected = nullptr;
    return true;
}

void FASTCALL hook_update(void *self, void *edx) {
    const int mx = *reinterpret_cast<int *>(kMouseX), my = *reinterpret_cast<int *>(kMouseY);
    xinput::Gamepad p = {};
    const bool have_pad = input::read_merged(&p);
    const float sx = have_pad ? axis(p.rx) : 0.0f, sy = have_pad ? axis(p.ry) : 0.0f;
    const bool latched = GetTickCount() < g_shoulder_until;
    const bool lb = (have_pad && (p.buttons & xinput::kLeftShoulder)) || (latched && g_shoulder_dir < 0);
    const bool rb = (have_pad && (p.buttons & xinput::kRightShoulder)) || (latched && g_shoulder_dir > 0);
    const bool zoom_out = lb || (have_pad && p.left_trigger > kTriggerDeadzone), zoom_in = rb || (have_pad && p.right_trigger > kTriggerDeadzone);

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
    if ((zoom_out || zoom_in) && !animating && !field<bool>(self, kToggleMode)) zoom_by(self, std::fmin(1.0f, std::fmax(-1.0f, (rb - lb) + trigger(p.right_trigger) - trigger(p.left_trigger))), dt);

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
        {"world_map_snap", "WorldMap::SnapCursor", 0x530A60, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83}, false,
         nfsmw::detour(&hook_snap_cursor), nfsmw::orig(&g_orig_snap_cursor)},
        {"world_map_zoom", "WorldMap::ScrollZoom", 0x552CB0, {0x83, 0xEC, 0x08, 0x56, 0x57, 0x8B, 0xF9}, false,
         nfsmw::detour(&hook_scroll_zoom), nfsmw::orig(&g_orig_scroll_zoom)},
    };
    nfsmw::install_hooks(hooks, 4, g_log, nullptr, "world map");
}

}  // namespace pad::world_map

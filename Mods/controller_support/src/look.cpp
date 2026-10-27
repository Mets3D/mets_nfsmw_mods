#include "bindings.h"
#include "controller_api.h"
#include "extra_device.h"
#include "hooks.h"
#include "input.h"
#include "look.h"
#include "look_options.h"
#include "view_ids.h"

namespace pad::look {

using namespace pad::bindings;

// Camera look. CubicCameraMover::Update (the chase cameras) builds the camera matrix with
// eCreateLookAtMatrix(out, from, to, up) at 0x47DCBC; that call is redirected here and the eye position (from) is
// turned around the target (to) by the look offsets. The mover's camera mode (+0x9C) is the view type (0 bumper,
// 1 hood, 2 close, 3 far, ...), +0xA1 is set while looking back (that view can be orbited too). Based on the widescreen fix's Camera.ixx (MIT,
// ThirteenAG), generalised to every chase view and fed from the game's bindings. Another player (another mod gave
// them a device: extra_device.h) looks around with the look bindings of that device, in their own view (view_ids.h: the view of the camera mover
// says whose it is); the first player's look and the mouse stay with the first.
// ------------------------------------------------------------------------------------------------
struct Vec3 {
    float x, y, z;
};
using LookAtFn = void (*)(void *, Vec3 *, Vec3 *, Vec3 *);
constexpr uintptr_t kLookAtCall = 0x47DCBC, kLookAt = 0x6CF0A0;

void *g_mover = nullptr;  // the CubicCameraMover being updated
float g_mover_dt = 0.0f;
int g_who = 0;  // whose it is: 0 to 3

struct LookState {
    float yaw = 0.0f, pitch = 0.0f;  // offsets in degrees
    float idle = 0.0f;
    Vec3 prev_target{};
    bool prev_target_valid = false;
} g_state[4];  // by player

// The look of a player with a device of their own (-1..1 each way): their device's look entries, with the same bindings and dead zone as anybody's.
void second_stick(int who, float *x, float *y) {
    const int slot = who - 1;
    *x = extra_device::value_slot(slot, kLookEntry[L_RIGHT]) - extra_device::value_slot(slot, kLookEntry[L_LEFT]);
    *y = extra_device::value_slot(slot, kLookEntry[L_UP]) - extra_device::value_slot(slot, kLookEntry[L_DOWN]);
}

using MoverUpdateFn = void(FASTCALL *)(void *, void *, float);
MoverUpdateFn g_orig_mover_update;
void FASTCALL hook_mover_update(void *self, void *edx, float dt) {
    g_mover = self;
    g_who = std::max(0, nfsmw::views::player_of_view(field<int>(self, 0x10)));  // CameraMover +0x10: its view
    g_mover_dt = dt;
    g_orig_mover_update(self, edx, dt);
    g_mover = nullptr;
}

// The mouse while driving. The game draws its own cursor whenever the mouse moves, and in a window the cursor can leave it. The look takes
// the mouse's movement from DirectInput, not from the cursor, so while the race HUD is up (FEngHud::Update ran in the last 200 ms and the
// game is not paused, which leaves the pause menu, the result screens and the front end with their cursor) the cursor is put back at the
// middle of the window every frame and the game's cursor is not drawn (the same test as the widescreen fix's).
DWORD g_last_hud_tick = 0;
bool driving() {
    constexpr uintptr_t kGameFlowManager = 0x925E70;
    const auto is_paused = reinterpret_cast<bool(__attribute__((thiscall)) *)(void *)>(0x64B680);  // GameFlowManager::IsPaused
    return g_last_hud_tick && GetTickCount() - g_last_hud_tick < 200 && !is_paused(reinterpret_cast<void *>(kGameFlowManager));
}

void recentre_cursor() {
    HWND window = *reinterpret_cast<HWND *>(0x982BF4);  // the game's window
    RECT client;
    if (!window || GetForegroundWindow() != window || !GetClientRect(window, &client)) return;
    POINT middle = {client.right / 2, client.bottom / 2};
    if (ClientToScreen(window, &middle)) SetCursorPos(middle.x, middle.y);
}

using HudUpdateFn = void(FASTCALL *)(void *, void *, unsigned, unsigned);
HudUpdateFn g_orig_hud_update;
void FASTCALL hook_hud_update(void *hud, void *edx, unsigned a, unsigned b) {
    g_last_hud_tick = GetTickCount();
    g_orig_hud_update(hud, edx, a, b);
}

using DrawCursorFn = void(__cdecl *)(int, int);
DrawCursorFn g_orig_draw_cursor;
void __cdecl hook_draw_cursor(int x, int y) {
    if (g_look_cfg.enabled && driving()) return;
    g_orig_draw_cursor(x, y);
}

// Whose camera the mouse looks around in: the player the keyboard drives (player 1, or the player of an extra device whose source is the keyboard), or nobody
// when every player is on a controller (-1; in a game of one player, whose vehicle source is every device, the mouse is player 1's).
int mouse_owner() {
    const int slot = extra_device::keyboard_slot();
    if (slot >= 0) return slot + 1;
    const int vehicle = input::vehicle_source();
    return vehicle == nfsmw::controller_api::kDefault || vehicle == nfsmw::controller_api::kSourceKeyboard ? 0 : -1;
}

void reset_look(int who) {
    LookState &l = g_state[who];
    l.yaw = l.pitch = l.idle = 0.0f;
    l.prev_target_valid = false;
}

// The look-at matrix of the camera of player `who` (see g_state) with the player's look offsets: `mode` is the view type (0 bumper, 1 hood, 2 close, ...).
void apply_look(int who, int mode, bool looking_back, float mover_dt, void *out, Vec3 *from, Vec3 *to, Vec3 *up) {
    const auto original = reinterpret_cast<LookAtFn>(kLookAt);
    LookState &l = g_state[who];
    LONG mouse_x = 0, mouse_y = 0;
    const int owner = mouse_owner();
    if (who == (owner < 0 ? 0 : owner)) {  // the mouse is the keyboard player's; with none, player 1's camera only keeps the cursor in the window
        input::take_mouse_delta(&mouse_x, &mouse_y);
        if (owner < 0) mouse_x = mouse_y = 0;
        if (g_look_cfg.enabled && mode >= 2 && driving()) recentre_cursor();
        // Mouse movement collected while no chase camera ran (menus, pause, other views) is dropped.
        static DWORD last_tick = 0;
        const DWORD now = GetTickCount();
        if ((mouse_x || mouse_y) && (now - last_tick > 250 || !l.prev_target_valid)) {
            static int dropped = 0;
            if (dropped++ < 3) log_line("look: mouse movement (%ld, %ld) dropped (%s)", mouse_x, mouse_y, !l.prev_target_valid ? "first frame of this view" : "no look update for 250 ms");
        }
        if (now - last_tick > 250 || !l.prev_target_valid) mouse_x = mouse_y = 0;
        last_tick = now;
    }
    // The look-back view is orbited like the others; switching between it and the normal view starts from the view's own angle.
    static bool was_looking_back[4];
    if (looking_back != was_looking_back[who]) {
        was_looking_back[who] = looking_back;
        reset_look(who);
    }
    if (!g_look_cfg.enabled || mode < 2) {
        reset_look(who);
        return original(out, from, to, up);
    }
    const float dt = mover_dt > 0.0f && mover_dt < 0.5f ? mover_dt : 1.0f / 60.0f;

    float car_speed = 0.0f;
    if (l.prev_target_valid) {
        const float dx = to->x - l.prev_target.x, dy = to->y - l.prev_target.y;
        car_speed = std::sqrt(dx * dx + dy * dy) / dt;
    }
    l.prev_target = *to;
    l.prev_target_valid = true;

    float stick_x, stick_y;
    if (who == 0) {
        stick_x = g_look_input[L_RIGHT] - g_look_input[L_LEFT];
        stick_y = g_look_input[L_UP] - g_look_input[L_DOWN];
    } else {
        second_stick(who, &stick_x, &stick_y);  // none without a controller
    }
    float yaw_delta = -stick_x * g_look_cfg.stick_speed * dt - mouse_x * g_look_cfg.mouse_sensitivity;
    // "Look Up" (stick_y > 0) raises the view, i.e. lowers the pitch offset; so does the mouse moving up (a negative count).
    float pitch_delta = -stick_y * g_look_cfg.stick_speed * dt + mouse_y * g_look_cfg.mouse_sensitivity;
    if (g_look_cfg.invert_y) pitch_delta = -pitch_delta;

    if (std::fabs(yaw_delta) > 0.001f || std::fabs(pitch_delta) > 0.001f) {
        l.yaw += yaw_delta;
        l.pitch += pitch_delta;
        l.idle = 0.0f;
    } else {
        l.idle += dt;
        const float delay = look_options::return_delay(who);  // 0 = never
        if (delay > 0.0f && l.idle >= delay && car_speed > 0.5f) {
            const float k = 1.0f - std::exp(-g_look_cfg.return_speed * dt);
            l.yaw -= l.yaw * k;
            l.pitch -= l.pitch * k;
            if (std::fabs(l.yaw) < 0.3f && std::fabs(l.pitch) < 0.3f) l.yaw = l.pitch = 0.0f;
        }
    }
    while (l.yaw > 180.0f) l.yaw -= 360.0f;
    while (l.yaw < -180.0f) l.yaw += 360.0f;
    if (l.yaw == 0.0f && l.pitch == 0.0f) return original(out, from, to, up);

    constexpr float kDeg = 0.0174532925f;
    const float dx = from->x - to->x, dy = from->y - to->y, dz = from->z - to->z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz), dist2d = std::sqrt(dx * dx + dy * dy);
    const float base_pitch = std::atan2(dz, dist2d) / kDeg;
    // Limit the offset so the view's own height angle stays within min/max pitch.
    if (base_pitch + l.pitch > g_look_cfg.max_pitch) l.pitch = g_look_cfg.max_pitch - base_pitch;
    if (base_pitch + l.pitch < g_look_cfg.min_pitch) l.pitch = g_look_cfg.min_pitch - base_pitch;
    const float yaw = std::atan2(dy, dx) + l.yaw * kDeg;
    const float pitch = (base_pitch + l.pitch) * kDeg;
    Vec3 eye = {to->x + dist * std::cos(pitch) * std::cos(yaw), to->y + dist * std::cos(pitch) * std::sin(yaw),
                to->z + dist * std::sin(pitch)};
    original(out, &eye, to, up);
}

// CubicCameraMover::Update's call of eCreateLookAtMatrix.
void look_at(void *out, Vec3 *from, Vec3 *to, Vec3 *up) {
    const int mode = g_mover ? field<int>(g_mover, 0x9C) : -1;
    const bool looking_back = g_mover && field<unsigned char>(g_mover, 0xA1);
    apply_look(g_who, mode, looking_back, g_mover_dt, out, from, to, up);
}

bool patch_look_call() {
    unsigned char *call = reinterpret_cast<unsigned char *>(kLookAtCall);
    const uintptr_t target = kLookAtCall + 5 + *reinterpret_cast<int32_t *>(call + 1);
    if (call[0] != 0xE8 || target != kLookAt) {
        log_line("look: the look-at call in CubicCameraMover::Update is already redirected (to %#x) - turn off the "
                 "widescreen fix's camera look ([CAMERA] Enable = 0); camera look off", static_cast<unsigned>(target));
        return false;
    }
    DWORD old = 0;
    VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old);
    *reinterpret_cast<int32_t *>(call + 1) =
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(&look_at) - (kLookAtCall + 5));
    VirtualProtect(call, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    return true;
}

void install_hooks() {
    const nfsmw::Hook hooks[] = {
        {"mover", "CubicCameraMover::Update", 0x47D3C0, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81}, false,
         nfsmw::detour(&hook_mover_update), nfsmw::orig(&g_orig_mover_update)},
        {"hud_update", "FEngHud::Update", 0x58CA30, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false,
         nfsmw::detour(&hook_hud_update), nfsmw::orig(&g_orig_hud_update)},
        {"draw_cursor", "DrawCursor", 0x56FDB0, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81}, false,
         nfsmw::detour(&hook_draw_cursor), nfsmw::orig(&g_orig_draw_cursor)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), g_log, nullptr, "camera look");
    if (g_look_cfg.enabled && !patch_look_call()) g_look_cfg.enabled = false;
}

}  // namespace pad::look

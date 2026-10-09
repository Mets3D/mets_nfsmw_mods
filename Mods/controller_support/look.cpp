#include "bindings.h"
#include "hooks.h"
#include "input.h"
#include "look.h"

namespace pad::look {

using namespace pad::bindings;

// Camera look. CubicCameraMover::Update (the chase cameras) builds the camera matrix with
// eCreateLookAtMatrix(out, from, to, up) at 0x47DCBC; that call is redirected here and the eye position (from) is
// turned around the target (to) by the look offsets. The mover's camera mode (+0x9C) is the view type (0 bumper,
// 1 hood, 2 close, 3 far, ...), +0xA1 is set while looking back. Based on the widescreen fix's Camera.ixx (MIT,
// ThirteenAG), generalised to every chase view and fed from the game's bindings.
// ------------------------------------------------------------------------------------------------
struct Vec3 {
    float x, y, z;
};
using LookAtFn = void (*)(void *, Vec3 *, Vec3 *, Vec3 *);
constexpr uintptr_t kLookAtCall = 0x47DCBC, kLookAt = 0x6CF0A0;

void *g_mover = nullptr;  // the CubicCameraMover being updated
float g_mover_dt = 0.0f;
float g_yaw = 0.0f, g_pitch = 0.0f;  // offsets in degrees
float g_idle = 0.0f;
Vec3 g_prev_target{};
bool g_prev_target_valid = false;

using MoverUpdateFn = void(FASTCALL *)(void *, void *, float);
MoverUpdateFn g_orig_mover_update;
void FASTCALL hook_mover_update(void *self, void *edx, float dt) {
    g_mover = self;
    g_mover_dt = dt;
    g_orig_mover_update(self, edx, dt);
    g_mover = nullptr;
}

void reset_look() {
    g_yaw = g_pitch = g_idle = 0.0f;
    g_prev_target_valid = false;
}

void look_at(void *out, Vec3 *from, Vec3 *to, Vec3 *up) {
    const auto original = reinterpret_cast<LookAtFn>(kLookAt);
    const int mode = g_mover ? field<int>(g_mover, 0x9C) : -1;
    const bool looking_back = g_mover && field<unsigned char>(g_mover, 0xA1);
    static int last_mode = -2;
    if (mode != last_mode) {
        log_line("look: camera mode %d%s", mode, mode >= 2 ? "" : " (no look)");
        last_mode = mode;
    }
    LONG mouse_x, mouse_y;
    input::take_mouse_delta(&mouse_x, &mouse_y);
    // Mouse movement collected while no chase camera ran (menus, pause, other views) is dropped.
    static DWORD last_tick = 0;
    const DWORD now = GetTickCount();
    if (now - last_tick > 250 || !g_prev_target_valid) mouse_x = mouse_y = 0;
    last_tick = now;
    if (!g_look_cfg.enabled || mode < 2 || looking_back) {
        reset_look();
        return original(out, from, to, up);
    }
    const float dt = g_mover_dt > 0.0f && g_mover_dt < 0.5f ? g_mover_dt : 1.0f / 60.0f;

    float car_speed = 0.0f;
    if (g_prev_target_valid) {
        const float dx = to->x - g_prev_target.x, dy = to->y - g_prev_target.y;
        car_speed = std::sqrt(dx * dx + dy * dy) / dt;
    }
    g_prev_target = *to;
    g_prev_target_valid = true;

    const float stick_x = g_look_input[L_RIGHT] - g_look_input[L_LEFT];
    const float stick_y = g_look_input[L_UP] - g_look_input[L_DOWN];
    float yaw_delta = -stick_x * g_look_cfg.stick_speed * dt - mouse_x * g_look_cfg.mouse_sensitivity;
    float pitch_delta = stick_y * g_look_cfg.stick_speed * dt - mouse_y * g_look_cfg.mouse_sensitivity;
    if (g_look_cfg.invert_y) pitch_delta = -pitch_delta;

    if (std::fabs(yaw_delta) > 0.001f || std::fabs(pitch_delta) > 0.001f) {
        g_yaw += yaw_delta;
        g_pitch += pitch_delta;
        g_idle = 0.0f;
    } else {
        g_idle += dt;
        if (g_idle >= g_look_cfg.return_delay && car_speed > 0.5f) {
            const float k = 1.0f - std::exp(-g_look_cfg.return_speed * dt);
            g_yaw -= g_yaw * k;
            g_pitch -= g_pitch * k;
            if (std::fabs(g_yaw) < 0.3f && std::fabs(g_pitch) < 0.3f) g_yaw = g_pitch = 0.0f;
        }
    }
    while (g_yaw > 180.0f) g_yaw -= 360.0f;
    while (g_yaw < -180.0f) g_yaw += 360.0f;
    if (g_yaw == 0.0f && g_pitch == 0.0f) return original(out, from, to, up);

    constexpr float kDeg = 0.0174532925f;
    const float dx = from->x - to->x, dy = from->y - to->y, dz = from->z - to->z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz), dist2d = std::sqrt(dx * dx + dy * dy);
    const float base_pitch = std::atan2(dz, dist2d) / kDeg;
    // Limit the offset so the view's own height angle stays within min/max pitch.
    if (base_pitch + g_pitch > g_look_cfg.max_pitch) g_pitch = g_look_cfg.max_pitch - base_pitch;
    if (base_pitch + g_pitch < g_look_cfg.min_pitch) g_pitch = g_look_cfg.min_pitch - base_pitch;
    const float yaw = std::atan2(dy, dx) + g_yaw * kDeg;
    const float pitch = (base_pitch + g_pitch) * kDeg;
    Vec3 eye = {to->x + dist * std::cos(pitch) * std::cos(yaw), to->y + dist * std::cos(pitch) * std::sin(yaw),
                to->z + dist * std::sin(pitch)};
    original(out, &eye, to, up);
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
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "camera look");
    if (g_look_cfg.enabled && !patch_look_call()) g_look_cfg.enabled = false;
}

}  // namespace pad::look

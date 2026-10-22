/*
 * The game has a free camera of its own, DebugWorldCameraMover (ctor 0x476440, Update 0x47E7B0): the camera director switches to
 * it with the action "CDActionDebug" (CameraAI::SetAction 0x479EB0; "CDActionDrive" goes back), which is what NFSMWExtraOptions'
 * Backspace does. Its own controls are an ActionQueue of config "DebugWorld" that nothing binds on PC, so we steer it ourselves.
 *
 * Update calls JoyHandler 0x47E4D0 every frame and then turns the globals Eye (0x8EE260) and Look (0x9110E0), both in the world's
 * swizzled axes (x forward, y left, z up), into the camera matrix; the heading and pitch it uses come from Look - Eye. So our
 * hook on JoyHandler moves Eye and sets Look = Eye + 20 * direction, and zeroes the mover's own movement fields (+0x80..+0x92)
 * and its turbo flags (0x911098, 0x91109C) after the original has drained the queue. Looking is the mouse (the game reads it
 * from Windows: recentred every frame, the delta is what it moved); moving is the player's own keyboard bindings (the binding
 * table 0x91F418, slots 0 and 1, type 1 = key with the DirectInput code in `device`): gas / brake forward / back, steer
 * right / left, shift up / down up / down, nitro faster, speedbreaker puts the player's car at the camera.
 *
 * While the camera is on, the player's InputPlayer (IInputPlayer, found as the AI mod does: the first local player's simable,
 * handle 0x403F80) is blocked so the car does not drive off with the same keys; BlockInput is vtable +4, IsBlocked +8, ClearInput +0x14.
 * Teleport is what DebugWorld::HandleJumpToHotPosition 0x755590 does: IVehicle (first of the players' list 0x92CD7C / 0x92CD84)
 * vtable +0x68 SetVehicleOnGround(position, direction), Sim::SetStream 0x6F1170 (position, true) and +0x94 SetSpeed. Those two
 * take the simulation's axes: sim = (-world.y, world.z, world.x).
 *
 * CameraAI::Update 0x480E40 (every frame, in the game thread) is where the keys are polled: F1 (the ini's toggle_key) switches
 * the camera on and off during a race.
 */
#include "free_camera.h"

#include <cmath>

#include "binding_table.h"
#include "dev_tools.h"
#include "hooks.h"
#include "keys.h"

namespace dev_tools::free_camera {

namespace {

constexpr uintptr_t kGameFlowState = 0x925E90;  // 6 = in the race
constexpr uintptr_t kEye = 0x8EE260, kLook = 0x9110E0, kTurbo = 0x911098, kSuperTurbo = 0x91109C;
constexpr uintptr_t kPlayerList = 0x92D87C, kPlayerVehicles = 0x92CD7C, kPlayerVehicleCount = 0x92CD84;
constexpr uintptr_t kSetAction = 0x479EB0, kSetStream = 0x6F1170, kFind = 0x5D59F0;
constexpr unsigned kIInputPlayerHandle = 0x403F80;
constexpr int kPlayerView = 1;  // EVIEW_PLAYER1
constexpr float kLookAhead = 20.0f;  // how far from the eye the look-at point is put
constexpr float kMaxPitch = 1.553f;  // 89 degrees

// Binding table entries (agent_docs/input-system.md)
constexpr int kGas = 0, kBrake = 1, kSteerLeft = 2, kSteerRight = 3, kSpeedbreaker = 5, kNitro = 6, kShiftDown = 7, kShiftUp = 8;

using GetFn = void *(__thiscall *)(void *);
using FindFn = void *(__thiscall *)(void *, void *);
using BlockFn = void(__thiscall *)(void *, int);
using BoolFn = bool(__thiscall *)(void *);
using VoidFn = void(__thiscall *)(void *);
using GroundFn = void(__thiscall *)(void *, const float *, const float *);
using SpeedFn = void(__thiscall *)(void *, float);

void(__cdecl *g_orig_camera_ai_update)(float);
void(__fastcall *g_orig_joy_handler)(void *, void *);

int g_toggle_vk = VK_F1;
float g_speed = 20.0f, g_fast_multiplier = 4.0f, g_sensitivity = 0.1f;  // metres per second, times, degrees per pixel

bool g_active = false;
bool g_flying = false;     // the mover has run once since the camera was turned on (yaw and pitch are taken from it then)
bool g_we_blocked = false;  // the player's input was not blocked before we did it
float g_yaw = 0, g_pitch = 0;
LARGE_INTEGER g_last_time;

struct Vec3 {
    float x, y, z;
};

// The keyboard slots of one of the game's actions, as the player has bound them.
bool action_down(int entry) {
    const nfsmw::binding_table::Slot *slots = nfsmw::binding_table::slots_of(nfsmw::binding_table::kLive, entry);
    for (int i = 0; i < 2; ++i) {
        if (slots[i].type != nfsmw::binding_table::kKey || slots[i].device <= 0) continue;
        const unsigned dik = static_cast<unsigned>(slots[i].device);
        const UINT scan = (dik & 0x7F) | ((dik & 0x80) ? 0xE000 : 0);
        if (nfsmw::key_down(static_cast<int>(MapVirtualKeyA(scan, MAPVK_VSC_TO_VK_EX)))) return true;
    }
    return false;
}

void set_camera_action(const char *name) { reinterpret_cast<void(__cdecl *)(int, const char *)>(kSetAction)(kPlayerView, name); }

// The first local player's IInputPlayer, null when there is none (fresh every time: a race restart makes new ones).
void *player_input() {
    void **list = *reinterpret_cast<void ***>(kPlayerList);
    if (!list || !list[0]) return nullptr;
    void *player = list[0];
    void *simable = reinterpret_cast<GetFn>((*static_cast<void ***>(player))[1])(player);
    if (!simable) return nullptr;
    return reinterpret_cast<FindFn>(kFind)(*reinterpret_cast<void **>(static_cast<char *>(simable) + 4), reinterpret_cast<void *>(kIInputPlayerHandle));
}

void block_player_input(bool block) {
    void *input = player_input();
    if (!input) return;
    void **vtable = *static_cast<void ***>(input);
    const bool blocked = reinterpret_cast<BoolFn>(vtable[2])(input);
    if (block && !blocked) {
        reinterpret_cast<BlockFn>(vtable[1])(input, 1);
        reinterpret_cast<VoidFn>(vtable[5])(input);  // ClearInput: no gas or steering left over from the last frame
        g_we_blocked = true;
    } else if (!block && g_we_blocked) {
        reinterpret_cast<BlockFn>(vtable[1])(input, 0);
        g_we_blocked = false;
    }
}

void activate() {
    g_active = true;
    g_flying = false;
    set_camera_action("CDActionDebug");
    block_player_input(true);
    g_log.line("free camera: on");
}

// `in_race`: the race is still there to give its camera and input back to (not when it has just ended).
void deactivate(bool in_race) {
    g_active = false;
    g_flying = false;
    if (in_race) {
        block_player_input(false);
        set_camera_action("CDActionDrive");
    }
    g_we_blocked = false;
    g_log.line("free camera: off");
}

void teleport_car() {
    const int count = *reinterpret_cast<int *>(kPlayerVehicleCount);
    void **list = *reinterpret_cast<void ***>(kPlayerVehicles);
    if (count < 1 || !list || !list[0]) return;
    void *vehicle = list[0];
    const float *eye = reinterpret_cast<const float *>(kEye);
    const float position[3] = {-eye[1], eye[2], eye[0]};
    const float direction[3] = {-sinf(g_yaw), 0.0f, cosf(g_yaw)};
    void **vtable = *static_cast<void ***>(vehicle);
    reinterpret_cast<GroundFn>(vtable[0x68 / 4])(vehicle, position, direction);
    reinterpret_cast<void(__cdecl *)(const float *, int)>(kSetStream)(position, 1);
    reinterpret_cast<SpeedFn>(vtable[0x94 / 4])(vehicle, 0.0f);
    g_log.line("free camera: the car is now at %.1f %.1f %.1f", eye[0], eye[1], eye[2]);
}

void look_with_mouse() {
    HWND window;
    if (!nfsmw::game_has_focus(&window)) return;
    RECT client;
    GetClientRect(window, &client);
    POINT centre = {client.right / 2, client.bottom / 2};
    ClientToScreen(window, &centre);
    POINT cursor;
    GetCursorPos(&cursor);
    SetCursorPos(centre.x, centre.y);
    if (!g_flying) return;  // the first frame only finds the centre
    const float radians = g_sensitivity * 3.14159265f / 180.0f;
    g_yaw -= static_cast<float>(cursor.x - centre.x) * radians;
    g_pitch -= static_cast<float>(cursor.y - centre.y) * radians;
    if (g_pitch > kMaxPitch) g_pitch = kMaxPitch;
    if (g_pitch < -kMaxPitch) g_pitch = -kMaxPitch;
}

void fly(char *mover) {
    float *eye = reinterpret_cast<float *>(kEye);
    float *look = reinterpret_cast<float *>(kLook);
    // The mover's own movement stays off
    memset(mover + 0x80, 0, 0x14);
    *reinterpret_cast<int *>(kTurbo) = 0;
    *reinterpret_cast<int *>(kSuperTurbo) = 0;

    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float dt = static_cast<float>(now.QuadPart - g_last_time.QuadPart) / static_cast<float>(frequency.QuadPart);
    g_last_time = now;
    if (dt < 0.0f || dt > 0.1f) dt = 0.0f;

    if (!g_flying) {
        const float dx = look[0] - eye[0], dy = look[1] - eye[1], dz = look[2] - eye[2];
        g_yaw = atan2f(dy, dx);
        g_pitch = atan2f(dz, sqrtf(dx * dx + dy * dy));
        g_log.line("free camera: at %.1f %.1f %.1f, heading %.0f degrees", eye[0], eye[1], eye[2], g_yaw * 180.0f / 3.14159265f);
    }
    look_with_mouse();
    g_flying = true;

    const bool focus = nfsmw::game_has_focus();
    const float ahead = focus ? (action_down(kGas) ? 1.0f : 0.0f) - (action_down(kBrake) ? 1.0f : 0.0f) : 0.0f;
    const float aside = focus ? (action_down(kSteerRight) ? 1.0f : 0.0f) - (action_down(kSteerLeft) ? 1.0f : 0.0f) : 0.0f;
    const float rise = focus ? (action_down(kShiftUp) ? 1.0f : 0.0f) - (action_down(kShiftDown) ? 1.0f : 0.0f) : 0.0f;
    const float cp = cosf(g_pitch), sp = sinf(g_pitch), cy = cosf(g_yaw), sy = sinf(g_yaw);
    const Vec3 forward = {cp * cy, cp * sy, sp};
    const Vec3 right = {sy, -cy, 0.0f};
    Vec3 move = {forward.x * ahead + right.x * aside, forward.y * ahead + right.y * aside, forward.z * ahead + rise};
    const float length = sqrtf(move.x * move.x + move.y * move.y + move.z * move.z);
    if (length > 1.0f) {
        move.x /= length;
        move.y /= length;
        move.z /= length;
    }
    const float speed = g_speed * (focus && action_down(kNitro) ? g_fast_multiplier : 1.0f) * dt;
    eye[0] += move.x * speed;
    eye[1] += move.y * speed;
    eye[2] += move.z * speed;
    look[0] = eye[0] + forward.x * kLookAhead;
    look[1] = eye[1] + forward.y * kLookAhead;
    look[2] = eye[2] + forward.z * kLookAhead;
}

void __fastcall hook_joy_handler(void *self, void *edx) {
    g_orig_joy_handler(self, edx);
    if (g_active) fly(static_cast<char *>(self));
}

void __cdecl hook_camera_ai_update(float dt) {
    static bool toggle_before = false, teleport_before = false;
    const bool in_race = *reinterpret_cast<int *>(kGameFlowState) == 6;
    const bool focus = nfsmw::game_has_focus();
    if (g_active && !in_race) deactivate(false);
    if (nfsmw::key_pressed(focus && nfsmw::key_down(g_toggle_vk), toggle_before) && in_race) {
        if (g_active) deactivate(true);
        else activate();
    }
    if (g_active) {
        block_player_input(true);  // the input objects are made again by a restart
        if (nfsmw::key_pressed(focus && action_down(kSpeedbreaker), teleport_before) && g_flying) teleport_car();
    }
    g_orig_camera_ai_update(dt);
}

}  // namespace

void install() {
    char key[16];
    GetPrivateProfileStringA("free_camera", "toggle_key", "F1", key, sizeof(key), g_ini);
    g_toggle_vk = nfsmw::key_from_name(key);
    char text[32];
    GetPrivateProfileStringA("free_camera", "speed", "20", text, sizeof(text), g_ini);
    g_speed = static_cast<float>(atof(text));
    GetPrivateProfileStringA("free_camera", "nitro_multiplier", "4", text, sizeof(text), g_ini);
    g_fast_multiplier = static_cast<float>(atof(text));
    GetPrivateProfileStringA("free_camera", "mouse_sensitivity", "0.1", text, sizeof(text), g_ini);
    g_sensitivity = static_cast<float>(atof(text));

    const nfsmw::Hook hooks[] = {
        {"cameraai_update", "CameraAI::Update", 0x480E40, {0x6A, 0xFF, 0x68, 0xD2, 0xA5, 0x86, 0x00}, true, nfsmw::detour(&hook_camera_ai_update),
         nfsmw::orig(&g_orig_camera_ai_update)},
        {"debug_joyhandler", "DebugWorldCameraMover::JoyHandler", 0x47E4D0, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83}, true, nfsmw::detour(&hook_joy_handler),
         nfsmw::orig(&g_orig_joy_handler)},
    };
    if (nfsmw::install_hooks(hooks, 2, g_log, g_ini, "free camera").ok)
        g_log.line("free camera: ready (key %d, %.0f m/s, x%.1f with nitro, mouse %.2f degrees per pixel)", g_toggle_vk, g_speed, g_fast_multiplier, g_sensitivity);
}

}  // namespace dev_tools::free_camera

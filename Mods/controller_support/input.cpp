// DirectInput controller state (dead zones, separate triggers, mouse deltas) and the game's input mapping (analog
// steering, gas and brake).
#include <MinHook.h>

#include <cwchar>
#include <cwctype>

#include "hooks.h"
#include "input.h"

namespace pad::input {

// ------------------------------------------------------------------------------------------------
// DirectInput: GetDeviceState (IDirectInputDevice8A vtable slot 9) is hooked in dinput8.dll itself, found through a
// keyboard device we create and release. That happens on the game's first input poll rather than at load time, so
// DirectInput is never started from inside the loader. DIJOYSTATE / DIJOYSTATE2 start with the same fields.
// ------------------------------------------------------------------------------------------------
struct JoyState {
    LONG x, y, z, rx, ry, rz;
    LONG slider[2];
    DWORD pov[4];
    BYTE buttons[32];
};
constexpr DWORD kJoyStateSize = 0x50, kJoyState2Size = 0x110;
constexpr LONG kAxisCentre = 32767;

const GUID kIID_IDirectInput8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
const GUID kGUID_SysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

using GetDeviceStateFn = HRESULT(WINAPI *)(void *, DWORD, void *);
using GetPropertyFn = HRESULT(WINAPI *)(void *, const GUID *, void *);
GetDeviceStateFn g_orig_get_device_state = nullptr;

// Which DirectInput devices are XInput controllers: their device path contains "IG_".
struct DeviceKind {
    void *device;
    bool xinput;
};
DeviceKind g_devices[16];
int g_device_count = 0;

bool is_xinput_device(void *device) {
    for (int i = 0; i < g_device_count; ++i)
        if (g_devices[i].device == device) return g_devices[i].xinput;
    struct {
        DWORD size, header_size, obj, how;  // DIPROPHEADER
        GUID guid_class;
        WCHAR path[MAX_PATH];
    } prop = {};
    prop.size = sizeof(prop);
    prop.header_size = 16;
    const GUID *kGuidAndPath = reinterpret_cast<const GUID *>(12);  // DIPROP_GUIDANDPATH
    const auto get_property = reinterpret_cast<GetPropertyFn>((*reinterpret_cast<void ***>(device))[5]);
    const HRESULT hr = get_property(device, kGuidAndPath, &prop);
    bool xinput = false;
    if (hr >= 0) {
        for (WCHAR *p = prop.path; *p; ++p) *p = towupper(*p);
        xinput = wcsstr(prop.path, L"IG_") != nullptr;
    }
    log_line("DirectInput controller %p: %s (path %ls)", device, xinput ? "XInput controller" : "other controller",
             hr >= 0 ? prop.path : L"?");
    if (g_device_count < 16) g_devices[g_device_count++] = {device, xinput};
    return xinput;
}

// Radial dead zone: inside it the stick reads exactly centred, outside it the range is rescaled to stay full.
void apply_deadzone(LONG *ax, LONG *ay, float deadzone) {
    const float x = (*ax - 32767.5f) / 32767.5f, y = (*ay - 32767.5f) / 32767.5f;
    const float r = std::sqrt(x * x + y * y);
    if (r <= deadzone) {
        *ax = *ay = kAxisCentre;
        return;
    }
    float scale = (r - deadzone) / (1.0f - deadzone) / r;
    if (r * scale > 1.0f) scale = 1.0f / r;
    *ax = static_cast<LONG>(lroundf(32767.5f + x * scale * 32767.5f));
    *ay = static_cast<LONG>(lroundf(32767.5f + y * scale * 32767.5f));
}

// Mouse movement since the camera look last read it (DIMOUSESTATE / DIMOUSESTATE2: lX, lY first).
volatile LONG g_mouse_dx = 0, g_mouse_dy = 0;
constexpr DWORD kMouseStateSize = 0x10, kMouseState2Size = 0x14;

HRESULT WINAPI hook_get_device_state(void *device, DWORD size, void *data) {
    const HRESULT hr = g_orig_get_device_state(device, size, data);
    if (hr >= 0 && data && (size == kMouseStateSize || size == kMouseState2Size)) {
        g_mouse_dx += static_cast<const LONG *>(data)[0];
        g_mouse_dy += static_cast<const LONG *>(data)[1];
        return hr;
    }
    if (hr < 0 || !data || (size != kJoyStateSize && size != kJoyState2Size) || !is_xinput_device(device)) return hr;
    JoyState &s = *static_cast<JoyState *>(data);

    static bool logged_raw = false;
    if (!logged_raw && (s.z < 30000 || s.z > 35500)) {
        logged_raw = true;
        log_line("raw trigger reading: z=%ld rz=%ld slider=%ld/%ld", s.z, s.rz, s.slider[0], s.slider[1]);
    }

    apply_deadzone(&s.x, &s.y, g_cfg.left_deadzone);
    apply_deadzone(&s.rx, &s.ry, g_cfg.right_deadzone);
    if (g_cfg.split_triggers) {
        xinput::Gamepad pad;
        if (xinput::read(&pad)) {
            s.z = kAxisCentre;
            s.slider[0] = kAxisCentre - pad.left_trigger * kAxisCentre / 255;
            s.slider[1] = kAxisCentre - pad.right_trigger * kAxisCentre / 255;
        }
    }
    return hr;
}

void hook_dinput() {
    HMODULE dinput = GetModuleHandleA("dinput8.dll");
    if (!dinput) dinput = LoadLibraryA("dinput8.dll");
    using CreateFn = HRESULT(WINAPI *)(HINSTANCE, DWORD, const GUID *, void **, void *);
    const auto create = dinput ? reinterpret_cast<CreateFn>(reinterpret_cast<void *>(GetProcAddress(dinput, "DirectInput8Create"))) : nullptr;
    void *di = nullptr, *keyboard = nullptr;
    if (!create || create(GetModuleHandleA(nullptr), 0x0800, &kIID_IDirectInput8A, &di, nullptr) < 0 || !di) {
        log_line("DirectInput8Create failed");
        return;
    }
    void **di_vtbl = *static_cast<void ***>(di);
    using CreateDeviceFn = HRESULT(WINAPI *)(void *, const GUID *, void **, void *);
    using ReleaseFn = ULONG(WINAPI *)(void *);
    if (reinterpret_cast<CreateDeviceFn>(di_vtbl[3])(di, &kGUID_SysKeyboard, &keyboard, nullptr) >= 0 && keyboard) {
        void *target = (*static_cast<void ***>(keyboard))[9];
        MH_STATUS s = MH_CreateHook(target, reinterpret_cast<void *>(&hook_get_device_state),
                                    reinterpret_cast<void **>(&g_orig_get_device_state));
        if (s == MH_OK) s = MH_EnableHook(target);
        log_line("GetDeviceState at %p: %s", target, s == MH_OK ? "hooked" : MH_StatusToString(s));
        reinterpret_cast<ReleaseFn>((*static_cast<void ***>(keyboard))[2])(keyboard);
    } else {
        log_line("CreateDevice(keyboard) failed");
    }
    reinterpret_cast<ReleaseFn>(di_vtbl[2])(di);
}

// ------------------------------------------------------------------------------------------------
// Input mapping: InputMapping(device, spec) reads the controller attributes into a list of entries
// (list head node at +4; node: next +0, entry +8 = {update type, lower dead zone, upper dead zone, action,
// scalar index, previous, current}). Update type 0 (kUpdate) passes the value through the dead zones as an analog
// value; 1/2 (press/release) turn it into 0 or 1 at 0.5, 3/4 into 0 or 1 at the lower dead zone.
// ------------------------------------------------------------------------------------------------
constexpr int kActionSteerLeft = 1, kActionSteerRight = 2, kActionGas = 5, kActionBrake = 6;

using MappingCtorFn = void *(FASTCALL *)(void *, void *, void *, void *);
MappingCtorFn g_orig_mapping_ctor;

void *FASTCALL hook_mapping_ctor(void *self, void *edx, void *device, void *spec) {
    void *result = g_orig_mapping_ctor(self, edx, device, spec);
    if (!g_cfg.analog_mapping) return result;
    void *head = field<void *>(self, 4);
    int changed = 0, seen = 0;
    for (void *node = head ? field<void *>(head, 0) : nullptr; node && node != head && seen < 512;
         node = field<void *>(node, 0), ++seen) {
        int &type = field<int>(node, 0x08);
        float &lower = field<float>(node, 0x0C);
        float &upper = field<float>(node, 0x10);
        const int action = field<int>(node, 0x14);
        if (action != kActionSteerLeft && action != kActionSteerRight && action != kActionGas && action != kActionBrake)
            continue;
        // Types 1-4 are the on/off kinds; anything else (kCenterControl = 5) is left alone.
        const bool convert = (type >= 1 && type <= 4) || (type == 0 && (lower != 0.0f || upper != 0.0f));
        static int logged = 0;
        if (logged++ < 32)
            log_line("mapping: action %d scalar %d type %d dead zones %.2f/%.2f%s", action, field<int>(node, 0x18), type,
                     lower, upper, convert ? " -> analog" : "");
        if (convert) {
            type = 0;
            lower = upper = 0.0f;
            ++changed;
        }
    }
    if (changed) log_line("mapping %p: %d steering/throttle entries made analog", self, changed);
    return result;
}

void take_mouse_delta(LONG *dx, LONG *dy) {
    *dx = InterlockedExchange(&g_mouse_dx, 0);
    *dy = InterlockedExchange(&g_mouse_dy, 0);
}

bool any_xinput_device() {
    for (int i = 0; i < g_device_count; ++i)
        if (g_devices[i].xinput) return true;
    return false;
}

bool only_xinput_controllers() {
    for (int i = 0; i < g_device_count; ++i)
        if (!g_devices[i].xinput) return false;
    return g_device_count > 0;
}

void install_hooks() {
    const nfsmw::Hook hooks[] = {
        {"InputMapping::InputMapping", "InputMapping::InputMapping", 0x6479D0,
         {0x6A, 0xFF, 0x68, 0xA8, 0x9E, 0x87, 0x00}, false, nfsmw::detour(&hook_mapping_ctor),
         nfsmw::orig(&g_orig_mapping_ctor)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "input");
}

}  // namespace pad::input

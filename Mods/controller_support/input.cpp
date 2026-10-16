// DirectInput controller state (dead zones, separate triggers, mouse deltas) and the game's input mapping (analog
// steering, gas and brake).
#include <MinHook.h>

#include <cstdlib>
#include <cwchar>
#include <cwctype>

#include "controller_api.h"
#include "hooks.h"
#include "device_settings.h"
#include "input.h"
#include "menu_buttons.h"
#include "pad_sets.h"

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

// Which DirectInput devices are XInput controllers: their device path contains "IG_". Steam Input's virtual controller
// (vendor 28DE, product 11FF) is one too: it is the pad's XInput controller as DirectInput shows it.
struct DeviceKind {
    void *device;
    bool xinput;
    char id[40];  // what tells this controller from another of the same kind (see identity_of_path), or empty
};
DeviceKind g_devices[16];
int g_device_count = 0;

// A device path is \\?\HID#VID_045E&PID_02DD&IG_00#<instance>#{guid}; Wine's instance is <version>&<serial>&<input>&..., and the serial
// is the controller's own (a Bluetooth controller's address) when it has one, else zeros or a counter. A controller that can be told
// apart gives "045E_02DD_<serial>": the serial must be 8 or more letters and digits, not all zeros. The path is upper case.
bool identity_of_path(const WCHAR *path, char *out, size_t size) {
    const WCHAR *vid = wcsstr(path, L"VID_"), *pid = wcsstr(path, L"PID_"), *hash = wcschr(path + (vid ? vid - path : 0), L'#');
    if (!vid || !pid || !hash) return false;
    const WCHAR *instance = hash + 1;
    const WCHAR *serial = wcschr(instance, L'&');
    if (!serial) return false;
    ++serial;
    size_t length = 0;
    bool nonzero = false;
    while (serial[length] && serial[length] != L'&' && serial[length] != L'#') {
        if (!iswalnum(serial[length])) return false;
        if (serial[length] != L'0') nonzero = true;
        ++length;
    }
    if (length < 8 || !nonzero || length > 24) return false;
    snprintf(out, size, "%.4ls_%.4ls_%.*ls", vid + 4, pid + 4, static_cast<int>(length), serial);
    return true;
}

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
    struct {
        DWORD size, header_size, obj, how;  // DIPROPHEADER
        DWORD data;
    } vid_pid = {};
    vid_pid.size = sizeof(vid_pid);
    vid_pid.header_size = 16;
    const GUID *kVidPid = reinterpret_cast<const GUID *>(24);  // DIPROP_VIDPID: product << 16 | vendor
    const bool have_ids = get_property(device, kVidPid, &vid_pid) >= 0;
    const bool steam_virtual = have_ids && vid_pid.data == 0x11FF28DE;
    xinput = xinput || steam_virtual;
    log_line("DirectInput controller %p: %s (vendor %04lx product %04lx, path %ls)", device,
             xinput ? (steam_virtual ? "XInput controller (Steam's virtual controller)" : "XInput controller") : "other controller",
             have_ids ? vid_pid.data & 0xFFFF : 0, have_ids ? vid_pid.data >> 16 : 0, hr >= 0 ? prop.path : L"?");
    DeviceKind kind = {device, xinput, ""};
    if (xinput && !steam_virtual && hr >= 0 && identity_of_path(prop.path, kind.id, sizeof(kind.id)))
        log_line("DirectInput controller %p can be told apart: %s", device, kind.id);
    if (g_device_count < 16) g_devices[g_device_count++] = kind;
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

// Whose dead zones a controller gets: its own slot's, or with a single XInput controller (whose slot the game cannot tell) the one in use.
int settings_slot(int own) { return own >= 0 ? own : (xinput::port >= 0 ? xinput::port : 0); }

// A trigger's dead zone: the same, on 0..255.
void apply_trigger_deadzone(xinput::Gamepad *pad, float deadzone) {
    const auto one = [deadzone](BYTE v) {
        return static_cast<BYTE>(lroundf(255.0f * ramp(v / 255.0f, deadzone, 1.0f)));
    };
    pad->left_trigger = one(pad->left_trigger);
    pad->right_trigger = one(pad->right_trigger);
}

// Mouse movement since the camera look last read it (DIMOUSESTATE / DIMOUSESTATE2: lX, lY, lZ, then the buttons).
volatile LONG g_mouse_dx = 0, g_mouse_dy = 0;
void *g_own_mouse = nullptr;          // the DirectInput mouse of our own (see poll_own_mouse), if there is one
volatile bool g_other_mouse_seen = false;  // some other device has been read as a mouse (the widescreen fix does)
constexpr DWORD kMouseStateSize = 0x10, kMouseState2Size = 0x14, kKeyboardStateSize = 256;

// The game's own controller polling is hooked (not DirectInput's function): see install_game_level_joystick_hook.
bool g_joystick_hooked_in_game = false;

// What the player touched last: a controller, or the keyboard or mouse (the button icons follow it).
volatile bool g_last_input_pad = false;
volatile int g_last_pad_slot = -1;

bool any_pressed(const BYTE *states, int count) {
    for (int i = 0; i < count; ++i)
        if (states[i] & 0x80) return true;
    return false;
}

// A button down, a stick away from the centre, or the hat pressed; for an XInput controller also a trigger (the
// triggers of other controllers can be anywhere at rest).
bool pad_active(const JoyState &s, bool xinput) {
    constexpr LONG kMargin = 1500;  // about 5% of the range
    const LONG sticks[] = {s.x, s.y, s.rx, s.ry};
    for (const LONG stick : sticks)
        if (std::labs(stick - kAxisCentre) > kMargin) return true;
    if (xinput) {
        if (std::labs(s.z - kAxisCentre) > kMargin) return true;
        for (const LONG slider : s.slider)
            if (kAxisCentre - slider > kMargin) return true;  // triggers (split into the sliders) pull towards 0
    }
    return any_pressed(s.buttons, 32) || (s.pov[0] & 0xFFFF) != 0xFFFF;
}

bool last_input_was_pad() { return g_last_input_pad; }
int last_pad_slot() { return g_last_pad_slot; }

// ------------------------------------------------------------------------------------------------
// Which controllers reach the game: by default all XInput controllers merged, or another mod asks for one (or none)
// through common/controller_api.h. Every XInput controller DirectInput shows the game reports the chosen XInput slot's
// state instead of its own, all slots merged, or a controller at rest. The game reads its controller by
// DirectInput device, and which DirectInput device is which XInput slot cannot be told; reporting the same state on all
// of them makes that not matter.
// ------------------------------------------------------------------------------------------------
volatile LONG g_vehicle_source = nfsmw::controller_api::kDefault;  // see nfsmw_controller_support_vehicle_source

// Whether the keyboard reaches the game, and the keyboard as DirectInput last showed it, wanted or not.
BYTE g_real_keyboard[256];
volatile LONG g_keyboard_seen = 0;

bool in_use(const xinput::Gamepad &p) {
    return p.buttons || std::abs(p.lx) > 12000 || std::abs(p.ly) > 12000 || std::abs(p.rx) > 12000 || std::abs(p.ry) > 12000 ||
           p.left_trigger > 40 || p.right_trigger > 40;
}

// The XInput slot of one of the game's XInput controllers: they are the connected slots in order. -1 when there is only
// one such controller (or this is not one): they then share the merged state.
int own_slot_unlogged(void *device) {
    // Which XInput slots have a controller, looked at again every 2 seconds (asking about an empty slot is slow).
    static bool connected[4];
    static DWORD next_scan = 0;
    const DWORD now = GetTickCount();
    const bool scan = static_cast<LONG>(now - next_scan) >= 0;
    if (scan) {
        next_scan = now + 2000;
        for (int slot = 0; slot < 4; ++slot) {
            xinput::Gamepad p;
            connected[slot] = xinput::read_slot(slot, &p);
        }
    }
    int rank = 0, mine = -1;
    for (int k = 0, n = pad_sets::joystick_count(); k < n; ++k) {
        void *other = pad_sets::joystick_di_device(k);
        if (!other || !is_xinput_device(other)) continue;
        if (other == device) mine = rank;
        ++rank;
    }
    if (mine < 0 || rank < 2) return -1;
    for (int slot = 0; slot < 4; ++slot)
        if (connected[slot] && mine-- == 0) return slot;
    return -1;
}

int own_slot(void *device) {
    const int slot = own_slot_unlogged(device);
    static void *seen[8];
    static int slots[8];
    static int count = 0;
    for (int i = 0; i < count; ++i)
        if (seen[i] == device) {
            if (slots[i] != slot) log_line("controller %d reports XInput controller %d now", pad_sets::joystick_of_device(device) + 1, slot + 1);
            slots[i] = slot;
            return slot;
        }
    if (count < 8) {
        seen[count] = device;
        slots[count++] = slot;
        log_line("controller %d reports XInput controller %d", pad_sets::joystick_of_device(device) + 1, slot + 1);
    }
    return slot;
}

int xinput_slot_of_joystick(int index) {
    void *device = pad_sets::joystick_di_device(index);
    return device ? own_slot_unlogged(device) : -1;
}

// The XInput controller (0-3) a game controller rumbles through: its own slot, or with a single XInput controller (whose slot the
// game cannot tell) the one in use; -1 for a controller that is not XInput.
int rumble_slot_of_joystick(int index) {
    void *device = pad_sets::joystick_di_device(index);
    if (!device || !is_xinput_device(device)) return -1;
    const int slot = own_slot_unlogged(device);
    if (slot >= 0) return slot;
    return xinput::port >= 0 ? xinput::port : 0;
}

// What tells the controller in an XInput slot from another of the same kind ("045E_02DD_<serial>"), or false when nothing does (the slot
// is all there is: a controller that has no serial of its own, Steam's virtual controller, a slot with nothing in it).
bool identity_of_slot(int slot, char *out, size_t size) {
    const char *single = nullptr;  // the id of the only XInput controller, if there is one
    int xinput_devices = 0;
    for (int k = 0, n = pad_sets::joystick_count(); k < n; ++k) {
        void *device = pad_sets::joystick_di_device(k);
        if (!device || !is_xinput_device(device)) continue;
        ++xinput_devices;
        const char *id = "";
        for (int i = 0; i < g_device_count; ++i)
            if (g_devices[i].device == device) id = g_devices[i].id;
        single = id;
        if (id[0] && own_slot_unlogged(device) == slot) {
            snprintf(out, size, "%s", id);
            return true;
        }
    }
    // A single XInput controller: the game cannot tell which slot it is, it is the one in use.
    if (xinput_devices == 1 && single[0] && slot == settings_slot(-1)) {
        snprintf(out, size, "%s", single);
        return true;
    }
    return false;
}

// All connected XInput controllers as one: any button, each stick axis and trigger from the pad pushing it furthest.
// An empty slot is asked about again every 2 seconds at most (that is slow).
bool read_merged(xinput::Gamepad *out) {
    return xinput::read_any(out, [](int slot, const xinput::Gamepad &p) {
        if (in_use(p)) xinput::port = slot;  // the controller in use is the one that rumbles
    });
}

void report_at_rest(JoyState &s) {
    s.x = s.y = s.z = s.rx = s.ry = s.rz = kAxisCentre;
    s.slider[0] = s.slider[1] = kAxisCentre;
    for (DWORD &pov : s.pov) pov = 0xFFFFFFFF;
    memset(s.buttons, 0, sizeof(s.buttons));
}

// The XInput state as DirectInput shows an XInput controller: sticks 0..65535 with Y down, both triggers on Z, buttons
// A B X Y LB RB Back Start LS RS, the D-pad as the first hat (hundredths of a degree, clockwise from up).
void report_xinput(JoyState &s, const xinput::Gamepad &p) {
    report_at_rest(s);
    s.x = p.lx + 32768L;
    s.y = 32767L - p.ly;
    s.rx = p.rx + 32768L;
    s.ry = 32767L - p.ry;
    s.z = kAxisCentre + (p.left_trigger - p.right_trigger) * 128L;
    const WORD order[10] = {xinput::kButtonA, xinput::kButtonB, xinput::kButtonX, xinput::kButtonY, xinput::kLeftShoulder,
                            xinput::kRightShoulder, xinput::kBack, xinput::kStart, xinput::kLeftThumb, xinput::kRightThumb};
    for (int i = 0; i < 10; ++i) s.buttons[i] = (p.buttons & order[i]) ? 0x80 : 0;
    const bool up = p.buttons & xinput::kDpadUp, down = p.buttons & xinput::kDpadDown;
    const bool left = p.buttons & xinput::kDpadLeft, right = p.buttons & xinput::kDpadRight;
    if (up && right) s.pov[0] = 4500;
    else if (down && right) s.pov[0] = 13500;
    else if (down && left) s.pov[0] = 22500;
    else if (up && left) s.pov[0] = 31500;
    else if (up) s.pov[0] = 0;
    else if (right) s.pov[0] = 9000;
    else if (down) s.pov[0] = 18000;
    else if (left) s.pov[0] = 27000;
}

// A controller's state, as the game is about to use it (called from the game's own polling code, see below, or from the
// DirectInput hook when that cannot be done): the dead zones, the triggers as two axes, what the player touched last, and
// the buttons of the menus' hotkeys.
void process_pad_state(void *device, JoyState *state) {
    JoyState &s = *state;
    if (!is_xinput_device(device)) {
        if (pad_active(s, false)) {
            g_last_input_pad = true;
            g_last_pad_slot = -1;
        }
        return;
    }
    // Each XInput controller reports its own state (every controller has bindings of its own); a single one, or none that
    // can be told apart, gets the merged state.
    xinput::Gamepad chosen = {};
    const bool replaced = g_cfg.use_all_xinput_controllers;
    const int own = own_slot(device);
    int dead_slot = own;  // whose dead zones: the controller's own, or with a single one (whose slot the game cannot tell) the one in use
    if (replaced) {
        const int slot = own;
        if (!(slot >= 0 ? xinput::read_slot(slot, &chosen) : read_merged(&chosen))) return report_at_rest(s);
        if (slot >= 0 && in_use(chosen)) xinput::port = slot;  // the controller in use is the one that rumbles
        dead_slot = settings_slot(slot);
        apply_trigger_deadzone(&chosen, device_settings::deadzones(dead_slot).trigger);
        report_xinput(s, chosen);
    } else {
        dead_slot = settings_slot(own);
    }

    const device_settings::Deadzones &dead = device_settings::deadzones(dead_slot);
    apply_deadzone(&s.x, &s.y, dead.left_stick);
    apply_deadzone(&s.rx, &s.ry, dead.right_stick);
    if (g_cfg.split_triggers) {
        // The triggers are the device's own controller's (not the one in use: with several, each reports its own).
        xinput::Gamepad pad = chosen;
        if (replaced || (own >= 0 ? xinput::read_slot(own, &pad) : xinput::read(&pad))) {
            if (!replaced) apply_trigger_deadzone(&pad, dead.trigger);
            s.z = kAxisCentre;
            s.slider[0] = kAxisCentre - pad.left_trigger * kAxisCentre / 255;
            s.slider[1] = kAxisCentre - pad.right_trigger * kAxisCentre / 255;
        }
    }
    device_settings::refresh();
    if (pad_active(s, true)) {
        g_last_input_pad = true;
        g_last_pad_slot = dead_slot;
    }
    menu_buttons::pad_buttons(pad_sets::joystick_of_device(device), s.buttons);
}

// A controller's state as the game gets it from this controller (dead zones, triggers as two axes), for bindings read
// without the game's polling (a second player's).
JoyState state_of_pad(xinput::Gamepad p, int slot) {
    const device_settings::Deadzones &dead = device_settings::deadzones(slot);
    apply_trigger_deadzone(&p, dead.trigger);
    JoyState s = {};
    report_xinput(s, p);
    apply_deadzone(&s.x, &s.y, dead.left_stick);
    apply_deadzone(&s.rx, &s.ry, dead.right_stick);
    if (g_cfg.split_triggers) {
        s.z = kAxisCentre;
        s.slider[0] = kAxisCentre - p.left_trigger * kAxisCentre / 255;
        s.slider[1] = kAxisCentre - p.right_trigger * kAxisCentre / 255;
    }
    return s;
}

// What a controller binding {type, object} (see bindings.h) reads from a controller: 0..1.
float binding_value(int type, int object, const xinput::Gamepad &p, int slot) {
    const JoyState s = state_of_pad(p, slot);
    const LONG axes[] = {s.x, s.y, s.z, s.rx, s.ry, s.rz};
    const auto low = [](LONG a) { return clamp01((kAxisCentre - a) / 32767.0f); };
    const auto high = [](LONG a) { return clamp01((a - kAxisCentre) / 32767.0f); };
    switch (type) {
        case 2: return object >= 0 && object < 32 && (s.buttons[object] & 0x80) ? 1.0f : 0.0f;
        case 3: {
            const DWORD pov = s.pov[0] & 0xFFFF;
            if (pov == 0xFFFF || object < 0 || object > 3) return 0.0f;
            const int angle = object * 9000;  // up, right, down, left
            const int diff = std::abs(static_cast<int>(pov) - angle);
            return std::min(diff, 36000 - diff) <= 4500 ? 1.0f : 0.0f;
        }
        case 4: return object >= 0 && object < 3 ? low(axes[object]) : 0.0f;
        case 5: return object >= 0 && object < 3 ? high(axes[object]) : 0.0f;
        case 6: return object >= 0 && object < 3 ? low(axes[3 + object]) : 0.0f;
        case 7: return object >= 0 && object < 3 ? high(axes[3 + object]) : 0.0f;
        case 8: return object >= 0 && object < 2 ? low(s.slider[object]) : 0.0f;
        case 9: return object >= 0 && object < 2 ? high(s.slider[object]) : 0.0f;
        default: return 0.0f;
    }
}

void __cdecl process_pad_state_from_game(void *device, JoyState *state) { process_pad_state(device, state); }

// The game reads a controller in sub_7F28A4 (GameDevice's DirectInput controller): it calls the controller's
// GetDeviceState through the device object's own table, into a DIJOYSTATE2 on its stack (at ebp-0x13C), and goes on with
// that. A controller that does not go through dinput8's own function (Steam Input's virtual one does not) would never
// reach a hook on DirectInput, so the state is processed here instead, right after the call: the instruction after it,
// `mov eax, [esi+0x464]` (6 bytes), is replaced by a jump to a few instructions of our own that call process_pad_state
// with the device (esi+0x468) and the state, and then do what it did. (The widescreen fix hooks the same function, a few
// instructions further on.)
bool install_game_level_joystick_hook() {
    constexpr uintptr_t kSite = 0x7F2927;
    static const unsigned char kExpected[6] = {0x8B, 0x86, 0x64, 0x04, 0x00, 0x00};  // mov eax, [esi+0x464]
    if (memcmp(reinterpret_cast<void *>(kSite), kExpected, sizeof(kExpected)) != 0) {
        log_line("controller state hook: the game's code at %#x was changed by another mod; using DirectInput instead",
                 static_cast<unsigned>(kSite));
        return false;
    }
    unsigned char *code = static_cast<unsigned char *>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!code) return false;
    unsigned char *c = code;
    const auto emit = [&c](std::initializer_list<unsigned char> bytes) {
        for (const unsigned char b : bytes) *c++ = b;
    };
    const auto emit_rel32 = [&c](unsigned char opcode, const void *target) {
        *c++ = opcode;
        const intptr_t rel = reinterpret_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(c + 4);
        memcpy(c, &rel, 4);
        c += 4;
    };
    emit({0x9C, 0x60});                                // pushfd, pushad
    emit({0x8B, 0x86, 0x68, 0x04, 0x00, 0x00});        // mov eax, [esi+0x468]   the DirectInput device
    emit({0x8D, 0x8D, 0xC4, 0xFE, 0xFF, 0xFF});        // lea ecx, [ebp-0x13C]   the state the game just read
    emit({0x51, 0x50});                                // push ecx, push eax
    emit_rel32(0xE8, reinterpret_cast<const void *>(&process_pad_state_from_game));  // call
    emit({0x83, 0xC4, 0x08});                          // add esp, 8
    emit({0x61, 0x9D});                                // popad, popfd
    emit({0x8B, 0x86, 0x64, 0x04, 0x00, 0x00});        // mov eax, [esi+0x464]   what the game had here
    emit_rel32(0xE9, reinterpret_cast<const void *>(kSite + 6));                     // jmp back
    FlushInstructionCache(GetCurrentProcess(), code, 64);

    DWORD old = 0;
    VirtualProtect(reinterpret_cast<void *>(kSite), 6, PAGE_EXECUTE_READWRITE, &old);
    unsigned char *site = reinterpret_cast<unsigned char *>(kSite);
    site[0] = 0xE9;
    const intptr_t rel = reinterpret_cast<intptr_t>(code) - static_cast<intptr_t>(kSite + 5);
    memcpy(site + 1, &rel, 4);
    site[5] = 0x90;
    VirtualProtect(reinterpret_cast<void *>(kSite), 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 6);
    log_line("controller state hook: installed in the game's controller polling (%#x)", static_cast<unsigned>(kSite));
    return true;
}

HRESULT WINAPI hook_get_device_state(void *device, DWORD size, void *data) {
    const HRESULT hr = g_orig_get_device_state(device, size, data);
    if (hr >= 0 && data && (size == kMouseStateSize || size == kMouseState2Size)) {
        if (device != g_own_mouse) g_other_mouse_seen = true;
        const LONG *mouse = static_cast<const LONG *>(data);
        g_mouse_dx += mouse[0];
        g_mouse_dy += mouse[1];
        if (mouse[0] || mouse[1] || mouse[2] || any_pressed(static_cast<const BYTE *>(data) + 12, 4)) g_last_input_pad = false;
        return hr;
    }
    if (hr >= 0 && data && size == kKeyboardStateSize) {
        memcpy(g_real_keyboard, data, kKeyboardStateSize);
        g_keyboard_seen = 1;
        if (any_pressed(static_cast<const BYTE *>(data), kKeyboardStateSize)) g_last_input_pad = false;
        return hr;
    }
    if (hr < 0 || !data || (size != kJoyStateSize && size != kJoyState2Size)) return hr;
    if (!g_joystick_hooked_in_game) process_pad_state(device, static_cast<JoyState *>(data));
    return hr;
}

// What DirectInput and XInput have attached, for the log: when a controller is not recognised, this is where to look.
struct DeviceInstance {  // DIDEVICEINSTANCEA
    DWORD size;
    GUID instance, product;
    DWORD type;
    char instance_name[260], product_name[260];
    GUID ff_driver;
    WORD usage_page, usage;
};

BOOL WINAPI log_device(const DeviceInstance *d, void *) {
    // guidProduct = PID << 16 | VID, then a fixed tail
    log_line("DirectInput game controller: \"%s\" / \"%s\", vendor %04lx product %04lx, type %#lx", d->instance_name,
             d->product_name, d->product.Data1 & 0xFFFF, d->product.Data1 >> 16, d->type);
    return 1;  // DIENUM_CONTINUE
}

void log_attached_devices(void **di_vtbl, void *di) {
    using EnumDevicesFn = HRESULT(WINAPI *)(void *, DWORD, BOOL(WINAPI *)(const DeviceInstance *, void *), void *, DWORD);
    constexpr DWORD kClassGameControllers = 4, kAttachedOnly = 1;
    const HRESULT hr = reinterpret_cast<EnumDevicesFn>(di_vtbl[4])(di, kClassGameControllers, &log_device, nullptr, kAttachedOnly);
    if (hr < 0) log_line("DirectInput: EnumDevices failed (%#lx)", static_cast<unsigned long>(hr));
    for (int i = 0; i < 4 && xinput::get_state; ++i) {
        xinput::State state;
        const DWORD r = xinput::get_state(i, &state);
        log_line("XInput slot %d: %s", i, r == ERROR_SUCCESS ? "a controller" : r == ERROR_DEVICE_NOT_CONNECTED ? "empty" : "error");
    }
    log_line("Steam overlay in this process: %s", GetModuleHandleA("gameoverlayrenderer.dll") ? "yes" : "no");
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
    log_attached_devices(di_vtbl, di);
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
    if (!g_cfg.analog_input) return result;
    void *head = field<void *>(self, 4);
    int seen = 0;
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
        if (convert) {
            type = 0;
            lower = upper = 0.0f;
        }
    }
    return result;
}

// The vanilla game takes the mouse from Windows (the cursor of its menus), not from DirectInput, so there is no
// movement to look around with. Without another mod reading one, a DirectInput mouse of our own (shared, only while the
// game is in front) is read through the same hooked GetDeviceState, which adds its movement up like any other.
void poll_own_mouse() {
    static bool tried = false;
    static DWORD next_try = 0;
    if (g_other_mouse_seen) return;
    using GetStateFn = HRESULT(WINAPI *)(void *, DWORD, void *);
    if (!g_own_mouse) {
        if (tried && static_cast<LONG>(GetTickCount() - next_try) < 0) return;
        tried = true;
        next_try = GetTickCount() + 5000;
        HWND window = *reinterpret_cast<HWND *>(0x982BF4);  // the game's window
        HMODULE dinput = GetModuleHandleA("dinput8.dll");
        using CreateFn = HRESULT(WINAPI *)(HINSTANCE, DWORD, const GUID *, void **, void *);
        const auto create = dinput ? reinterpret_cast<CreateFn>(reinterpret_cast<void *>(GetProcAddress(dinput, "DirectInput8Create"))) : nullptr;
        void *di = nullptr, *mouse = nullptr;
        if (!window || !create || create(GetModuleHandleA(nullptr), 0x0800, &kIID_IDirectInput8A, &di, nullptr) < 0 || !di) return;
        // DIMOUSESTATE: X, Y, Z axes and four buttons (c_dfDIMouse, which dinput8 does not export).
        static const GUID kSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
        static const GUID kXAxis = {0xA36D02E0, 0xC9F3, 0x11CF, {0xBF, 0xC4, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
        static const GUID kYAxis = {0xA36D02E1, 0xC9F3, 0x11CF, {0xBF, 0xC4, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
        static const GUID kZAxis = {0xA36D02E2, 0xC9F3, 0x11CF, {0xBF, 0xC4, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
        struct ObjectFormat {
            const GUID *guid;
            DWORD offset, type, flags;
        };
        constexpr DWORD kAxis = 0x3, kButton = 0xC, kAnyInstance = 0xFFFF00, kOptional = 0x80000000;  // DIDFT_*
        static const ObjectFormat objects[] = {
            {&kXAxis, 0, kAxis | kAnyInstance, 0}, {&kYAxis, 4, kAxis | kAnyInstance, 0},
            {&kZAxis, 8, kOptional | kAxis | kAnyInstance, 0}, {nullptr, 12, kButton | kAnyInstance, 0},
            {nullptr, 13, kButton | kAnyInstance, 0}, {nullptr, 14, kButton | kOptional | kAnyInstance, 0},
            {nullptr, 15, kButton | kOptional | kAnyInstance, 0}};
        struct DataFormat {
            DWORD size, object_size, flags, data_size, object_count;
            const ObjectFormat *objects;
        } format = {sizeof(DataFormat), sizeof(ObjectFormat), 2 /* DIDF_RELAXIS */, 16, 7, objects};
        void **di_vtbl = *static_cast<void ***>(di);
        using CreateDeviceFn = HRESULT(WINAPI *)(void *, const GUID *, void **, void *);
        using SetFormatFn = HRESULT(WINAPI *)(void *, const void *);
        using SetLevelFn = HRESULT(WINAPI *)(void *, HWND, DWORD);
        using ReleaseFn = ULONG(WINAPI *)(void *);
        const bool made = reinterpret_cast<CreateDeviceFn>(di_vtbl[3])(di, &kSysMouse, &mouse, nullptr) >= 0 && mouse;
        if (made) {
            void **vtbl = *static_cast<void ***>(mouse);
            const bool ok = reinterpret_cast<SetFormatFn>(vtbl[11])(mouse, &format) >= 0 &&
                            reinterpret_cast<SetLevelFn>(vtbl[13])(mouse, window, 2 | 4) >= 0;  // non-exclusive, foreground
            if (ok) {
                g_own_mouse = mouse;
                log_line("mouse: reading it through our own DirectInput device (the game takes it from Windows)");
            } else {
                reinterpret_cast<ReleaseFn>(vtbl[2])(mouse);
                log_line("mouse: could not set up a DirectInput mouse");
            }
        } else {
            log_line("mouse: could not create a DirectInput mouse");
        }
        reinterpret_cast<ReleaseFn>(di_vtbl[2])(di);
        if (!g_own_mouse) return;
    }
    void **vtbl = *static_cast<void ***>(g_own_mouse);
    BYTE state[16];
    HRESULT hr = reinterpret_cast<GetStateFn>(vtbl[9])(g_own_mouse, sizeof(state), state);  // the hook adds the movement up
    if (hr == static_cast<HRESULT>(0x8007001E) || hr == static_cast<HRESULT>(0x8007000C)) {  // input lost / not acquired
        using AcquireFn = HRESULT(WINAPI *)(void *);
        reinterpret_cast<AcquireFn>(vtbl[7])(g_own_mouse);
    }
}

void take_mouse_delta(LONG *dx, LONG *dy) {
    poll_own_mouse();
    *dx = InterlockedExchange(&g_mouse_dx, 0);
    *dy = InterlockedExchange(&g_mouse_dy, 0);
}

int vehicle_source() { return g_vehicle_source; }
int player_one_rumble_slot() {
    const int source = g_vehicle_source;
    if (source == nfsmw::controller_api::kSourceKeyboard) return -1;
    return source >= 0 ? source : xinput::port;
}
bool key_down(int dik) { return dik > 0 && dik < 256 && (g_real_keyboard[dik] & 0x80) != 0; }

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
    g_joystick_hooked_in_game = install_game_level_joystick_hook();
    const nfsmw::Hook hooks[] = {
        {"InputMapping::InputMapping", "InputMapping::InputMapping", 0x6479D0,
         {0x6A, 0xFF, 0x68, 0xA8, 0x9E, 0x87, 0x00}, false, nfsmw::detour(&hook_mapping_ctor),
         nfsmw::orig(&g_orig_mapping_ctor)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "input");
}

}  // namespace pad::input

// For other mods (common/controller_api.h).
// Which device drives player 1's vehicle: an XInput controller (0-3), kSourceKeyboard, or kDefault (every device). The other
// devices still reach the game, and drive its menus; only the vehicle entries of the binding table (pad_sets.cpp) are
// taken from this device alone.
extern "C" __declspec(dllexport) void nfsmw_controller_support_vehicle_source(int source) {
    using namespace pad;
    if (InterlockedExchange(&input::g_vehicle_source, source) == source) return;
    if (source >= 0) log_line("player 1's vehicle: XInput controller %d only (asked for by another mod)", source + 1);
    else if (source == nfsmw::controller_api::kSourceKeyboard) log_line("player 1's vehicle: the keyboard only (asked for by another mod)");
    else log_line("player 1's vehicle: every device (asked for by another mod)");
}

extern "C" __declspec(dllexport) int nfsmw_controller_support_keyboard_state(unsigned char *out) {
    using namespace pad;
    if (!input::g_keyboard_seen || !out) return 0;
    memcpy(out, input::g_real_keyboard, 256);
    return 1;
}

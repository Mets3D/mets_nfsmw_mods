#include "new_controllers.h"

#include "bindings.h"
#include "hooks.h"
#include "input.h"
#include "pad_sets.h"

namespace pad::new_controllers {

using namespace pad::bindings;

constexpr uintptr_t kManager = 0x9203B4;     // the game's DirectInput wrapper: +0xB4 IDirectInput8
constexpr uintptr_t kAddDevice = 0x7F1A6B;   // its EnumDevices callback, stdcall (DIDEVICEINSTANCE*, manager): makes the game's object
                                             // for a device (a controller goes into the joystick table through the callback of GlobalInit)
constexpr uintptr_t kIoModule = 0x634370;    // IOModule::GetIOModule; +8 is the GameDevice of port 0
constexpr uintptr_t kControllerDefaults = 0x63CD80;  // thiscall (GameDevice): the controller bindings of the first controller's model
constexpr uintptr_t kStringHashUpper = 0x460BC0;
constexpr uintptr_t kFirstState = 0x91F1E8;  // the first controller's state at start-up (0x8C dwords), the Controls screen's rebinding compares with it
constexpr uintptr_t kOddState1 = 0x91F1EC, kOddState2 = 0x91F3FC;
constexpr unsigned kOddControllerHash = 0x5FB6CCBA;  // one model InitInputs treats specially
constexpr int kMaxJoysticks = 8;
constexpr int kProfileEntries = 0x13, kProfileBytes = 4 + kProfileEntries * 0x30;  // what the profile saves of the binding table

unsigned char g_profile[kProfileBytes];  // the profile's bindings as the game last loaded them
bool g_have_profile = false;

using BindingsIoFn = unsigned char *(__cdecl *)(unsigned char *, unsigned char *);
BindingsIoFn g_orig_bindings_io;

// UserProfile::BindingsIO_B(buffer, end): the profile's saved controller type and entries 0-18, copied over the table. Run again when
// the first controller arrives (it is what a start with that controller would have done).
unsigned char *__cdecl hook_bindings_io(unsigned char *buffer, unsigned char *end) {
    if (buffer && end - buffer >= kProfileBytes) {
        memcpy(g_profile, buffer, kProfileBytes);
        g_have_profile = true;
    }
    return g_orig_bindings_io(buffer, end);
}

void *game_device() { return *reinterpret_cast<void **>(reinterpret_cast<uintptr_t (*)()>(kIoModule)() + 8); }

// What InitInputs does for the first controller: remembers its resting state, then the defaults of its model; then the load of
// the profile puts the saved bindings over those, as it does at start-up.
void first_controller_arrived() {
    void *joystick = pad_sets::joystick_object(0);
    if (!joystick) return;
    void **vtbl = *static_cast<void ***>(joystick);
    using MethodFn0 = void *(FASTCALL *)(void *, void *);
    reinterpret_cast<MethodFn0>(vtbl[4])(joystick, nullptr);  // poll
    const void *state = reinterpret_cast<MethodFn0>(vtbl[1])(joystick, nullptr);
    if (state) memcpy(reinterpret_cast<void *>(kFirstState), state, 0x8C * 4);
    using HashFn = unsigned(__cdecl *)(const char *);
    if (reinterpret_cast<HashFn>(kStringHashUpper)(static_cast<const char *>(joystick) + 0x114) == kOddControllerHash) {
        *reinterpret_cast<unsigned *>(kOddState1) = 0xFFFF;
        *reinterpret_cast<unsigned *>(kOddState2) = 0xFFFF;
    }
    // The keyboard bindings are the player's own work this session: the profile's copy would take them back.
    static Slot keyboard[kProfileEntries][2];
    for (int e = 0; e < kProfileEntries; ++e)
        for (int s = 0; s < 2; ++s) keyboard[e][s] = slots_of(kBindingTable, e)[s];
    void *device = game_device();
    reinterpret_cast<MethodFn0>(kControllerDefaults)(device, nullptr);
    if (g_have_profile) g_orig_bindings_io(g_profile, g_profile + kProfileBytes);
    for (int e = 0; e < kProfileEntries; ++e)
        for (int s = 0; s < 2; ++s) slots_of(kBindingTable, e)[s] = keyboard[e][s];
    resync_changes();
    log_line("first controller: its resting state and the bindings for its model %s", g_have_profile ? "and the profile's saved ones are in place" : "are in place");
}

// The controllers the game has (and every one this has given it): one that is unplugged keeps its object in the game's list, and must not
// be given again when it is plugged back in, or asked about when DirectInput cannot answer for it any more.
struct Scan {
    GUID known[16];
    int known_count = 0;
    void *manager = nullptr;
    int added = 0;
    void remember(const GUID &guid) {
        for (int i = 0; i < known_count; ++i)
            if (memcmp(&known[i], &guid, sizeof(GUID)) == 0) return;
        if (known_count < 16) known[known_count++] = guid;
    }
};
Scan g_scan;

BOOL WINAPI add_if_new(const input::DeviceInstance *d, void *context) {
    Scan &scan = *static_cast<Scan *>(context);
    const unsigned type = d->type & 0xFF;
    if (type < 0x14 || type > 0x18) return 1;  // the game's own test: the types it makes controllers of
    for (int i = 0; i < scan.known_count; ++i)
        if (memcmp(&scan.known[i], &d->instance, sizeof(GUID)) == 0) return 1;
    if (pad_sets::joystick_count() >= kMaxJoysticks) return 0;
    log_line("new controller: \"%s\" (the game has %d)", d->instance_name, pad_sets::joystick_count());
    reinterpret_cast<BOOL(WINAPI *)(const input::DeviceInstance *, void *)>(kAddDevice)(d, scan.manager);
    scan.remember(d->instance);
    ++scan.added;
    return 1;
}

void check() {
    static DWORD next = 0;
    const DWORD now = GetTickCount();
    if (static_cast<LONG>(now - next) < 0) return;
    next = now + 2000;
    void *manager = *reinterpret_cast<void **>(kManager);
    void *di = manager ? field<void *>(manager, 0xB4) : nullptr;
    if (!di || pad_sets::joystick_count() >= kMaxJoysticks) return;
    Scan &scan = g_scan;
    scan.manager = manager;
    scan.added = 0;
    for (int i = 0; i < pad_sets::joystick_count(); ++i) {
        void *device = pad_sets::joystick_di_device(i);
        input::DeviceInstance info = {};
        info.size = sizeof(info);
        using GetInfoFn = HRESULT(WINAPI *)(void *, input::DeviceInstance *);
        if (device && reinterpret_cast<GetInfoFn>((*static_cast<void ***>(device))[15])(device, &info) >= 0)
            scan.remember(info.instance);
    }
    const int before = pad_sets::joystick_count();
    using EnumDevicesFn = HRESULT(WINAPI *)(void *, DWORD, BOOL(WINAPI *)(const input::DeviceInstance *, void *), void *, DWORD);
    constexpr DWORD kClassGameControllers = 4, kAttachedOnly = 1;
    reinterpret_cast<EnumDevicesFn>((*static_cast<void ***>(di))[4])(di, kClassGameControllers, &add_if_new, &scan, kAttachedOnly);
    if (!scan.added) return;
    log_line("the game now has %d controller(s)", pad_sets::joystick_count());
    if (before == 0 && pad_sets::joystick_count() > 0) first_controller_arrived();
    pad_sets::controllers_changed();
}

void install_hooks() {
    const nfsmw::Hook hooks[] = {
        {"bindings_io", "UserProfile::BindingsIO_B", 0x56E200, {0x51, 0x55, 0x56, 0xE8, 0x68, 0x61, 0x0C}, false,
         nfsmw::detour(&hook_bindings_io), nfsmw::orig(&g_orig_bindings_io)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "new controllers");
}

}  // namespace pad::new_controllers

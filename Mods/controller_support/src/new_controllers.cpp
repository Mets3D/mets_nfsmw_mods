#include "new_controllers.h"

#include "bindings.h"
#include "default_layout.h"
#include "hooks.h"
#include "input.h"
#include "pad_sets.h"
#include "patch.h"

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

void *game_device();

// A profile saved with the game's own default controller bindings (every profile that was saved before this mod, or never got ours) keeps
// them for ever: the bindings a profile saved win over the defaults. What the game's defaults give for the controller is known (they can be
// asked for, with our layout left out), so bindings that equal them were never chosen by the player: ours take their place. Bindings that
// differ in any entry are the player's, and stay.
void replace_vanilla_bindings() {
    using MethodFn0 = void *(FASTCALL *)(void *, void *);
    void *device = pad_sets::joystick_count() > 0 ? game_device() : nullptr;
    if (!device || !default_layout::enabled()) return;
    static Slot saved[kBindingCount][4], vanilla[kBindingCount][4];
    for (int e = 0; e < kBindingCount; ++e) memcpy(saved[e], slots_of(kBindingTable, e), sizeof(saved[e]));
    default_layout::suppress(true);
    reinterpret_cast<MethodFn0>(kControllerDefaults)(device, nullptr);
    default_layout::suppress(false);
    for (int e = 0; e < kBindingCount; ++e) {
        memcpy(vanilla[e], slots_of(kBindingTable, e), sizeof(vanilla[e]));
        memcpy(slots_of(kBindingTable, e), saved[e], sizeof(saved[e]));  // the profile's, as they were
    }
    for (int e = 0; e < kProfileEntries; ++e)
        for (int s = 2; s < 4; ++s)
            if (saved[e][s].type != vanilla[e][s].type || saved[e][s].object != vanilla[e][s].object) {
                bindings::resync_changes();
                return;
            }
    const int changed = default_layout::apply_to_live();
    bindings::resync_changes();
    log_line("the profile's controller bindings are the game's own defaults: our default layout replaces them (%d entries)", changed);
}

// UserProfile::BindingsIO_B(buffer, end): the profile's saved controller type and entries 0-18, copied over the table. Run again when
// the first controller arrives (it is what a start with that controller would have done).
unsigned char *__cdecl hook_bindings_io(unsigned char *buffer, unsigned char *end) {
    if (buffer && end - buffer >= kProfileBytes) {
        memcpy(g_profile, buffer, kProfileBytes);
        g_have_profile = true;
    }
    unsigned char *result = g_orig_bindings_io(buffer, end);
    replace_vanilla_bindings();
    return result;
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
    if (g_have_profile) {
        g_orig_bindings_io(g_profile, g_profile + kProfileBytes);
        replace_vanilla_bindings();
    }
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

// Only XInput controllers (Steam Input's virtual ones included) are supported: the bindings, the extra controllers' sets, the Settings page and
// the rumble all go by XInput. A controller of any other kind (a DualSense on Bluetooth, say) would be listed in the Controls menu and do
// nothing, so the game is not told about it; Steam Input turns such a controller into an XInput one.
struct Ignored {
    GUID guid;
    char name[80];
    bool logged;
};
Ignored g_ignored[16];
int g_ignored_count = 0;
bool g_log_open = false;  // the log is opened when the mod's main runs; the start-up list of controllers is asked about before that

void log_ignored() {
    for (int i = 0; i < g_ignored_count; ++i)
        if (!g_ignored[i].logged) {
            g_ignored[i].logged = true;
            log_line("controller \"%s\" is not an XInput controller: not supported, so the game is not given it (Steam Input makes any controller an XInput one)", g_ignored[i].name);
        }
}

bool supported(const input::DeviceInstance *d) {
    for (int i = 0; i < g_ignored_count; ++i)
        if (!memcmp(&g_ignored[i].guid, &d->instance, sizeof(GUID))) return false;
    if (input::is_xinput_instance(*d)) return true;
    if (g_ignored_count < 16) {
        Ignored &ignored = g_ignored[g_ignored_count++];
        ignored.guid = d->instance;
        snprintf(ignored.name, sizeof(ignored.name), "%s", d->instance_name);
        ignored.logged = false;
    }
    if (g_log_open) log_ignored();
    return false;
}

// The game lists its controllers once, at start-up, before this mod's main runs, so this runs from NFSMW_PLUGIN_EARLY: the start of the
// callback that makes the game's object for a device (0x7F1A6B, stdcall (DIDEVICEINSTANCE*, manager)) jumps to a stub that first asks
// skip_unsupported and, for a controller that is not supported, returns TRUE (go on with the next device) without making anything.
int __cdecl skip_unsupported(const input::DeviceInstance *d) {
    const unsigned type = d->type & 0xFF;
    return type >= 0x14 && type <= 0x18 && !supported(d);
}
bool g_filter_installed = false;

BOOL WINAPI add_if_new(const input::DeviceInstance *d, void *context) {
    Scan &scan = *static_cast<Scan *>(context);
    const unsigned type = d->type & 0xFF;
    if (type < 0x14 || type > 0x18) return 1;  // the game's own test: the types it makes controllers of
    if (!supported(d)) return 1;
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

void install_early() {
    static const unsigned char kExpected[7] = {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 0x08};  // push ebp; mov ebp,esp; push esi; mov esi,[ebp+8]
    nfsmw::Stub stub;
    stub.emit({0xFF, 0x74, 0x24, 0x04});                                        // push dword ptr [esp+4]   the device
    stub.emit_rel32(0xE8, reinterpret_cast<uintptr_t>(&skip_unsupported));      // call
    stub.emit({0x83, 0xC4, 0x04, 0x85, 0xC0});                                  // add esp,4; test eax,eax
    const size_t run = stub.jump_short(0x74);                                   // jz: the game makes its object
    stub.emit({0xB8, 0x01, 0x00, 0x00, 0x00, 0xC2, 0x08, 0x00});                // mov eax,1; ret 8
    stub.resolve_short(run);
    stub.emit({0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 0x08});                      // what the patched bytes did
    stub.emit_rel32(0xE9, kAddDevice + sizeof(kExpected));
    stub.done();
    g_filter_installed = nfsmw::jump_to_stub(kAddDevice, kExpected, sizeof(kExpected), stub);
}

void install_hooks() {
    g_log_open = true;
    log_line("controllers that are not XInput: %s", g_filter_installed ? "left out of the game's list" : "not left out (the game's code at 0x7F1A6B is not as expected)");
    log_ignored();
    const nfsmw::Hook hooks[] = {
        {"bindings_io", "UserProfile::BindingsIO_B", 0x56E200, {0x51, 0x55, 0x56, 0xE8, 0x68, 0x61, 0x0C}, false,
         nfsmw::detour(&hook_bindings_io), nfsmw::orig(&g_orig_bindings_io)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "new controllers");
}

}  // namespace pad::new_controllers

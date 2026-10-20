#include "extra_device.h"

#include "bindings.h"
#include "controller_api.h"
#include "input.h"
#include "pad_sets.h"

namespace pad::extra_device {

using namespace pad::bindings;

constexpr uintptr_t kIoModule = 0x920DE0;  // +4 device count, +8 devices (16 pointers), +0x48 bit mask of the devices
constexpr int kMaxDevices = 16;
constexpr uintptr_t kFactory = 0x63CD10;     // makes a GameDevice: cdecl, (port)
constexpr uintptr_t kStringHash = 0x5CC240;  // the game's name hash: cdecl, (const char *)
constexpr int kRecordsAt = 0x34, kPreviousAt = 0x1034, kCurrentAt = 0x1434;  // inside a GameDevice, 0x400 bytes of floats

void *g_device = nullptr;
int g_port = -1;
volatile LONG g_source = nfsmw::controller_api::kSourceNone;

int port() { return g_port; }
int port_for_player(int joystick_port) {
    return joystick_port == -1 && g_device && g_source != nfsmw::controller_api::kSourceNone ? g_port : joystick_port;
}
bool is_extra(void *device) { return g_device && device == g_device; }
int source_slot() { return g_source >= 0 ? g_source : -1; }
bool keyboard_source() { return g_source == nfsmw::controller_api::kSourceKeyboard; }
float value(int entry) {
    if (!g_device || entry < 0 || entry >= kBindingCount) return 0.0f;
    return field<float *>(g_device, 0x20)[entry];
}

// The game makes its first GameDevice with all its tables; a later one only gets the fields (the constructor skips them),
// and the port-0-only initialisation that links an input to a value (GameDevice vtable 3) is not run for it. Both are
// done here the way the game does them for port 0: 76 inputs, one per binding entry, each pointing at its value.
void set_up_tables(char *device) {
    field<char *>(device, 0x18) = device + kRecordsAt;
    field<char *>(device, 0x1C) = device + kPreviousAt;
    field<char *>(device, 0x20) = device + kCurrentAt;
    memset(device + kPreviousAt, 0, 0x400);
    memset(device + kCurrentAt, 0, 0x400);
    field<unsigned char>(device, 0x183C) = 0;
    field<int>(device, 0x1840) = 1;
    field<int>(device, 0x1844) = 0;
    field<int>(device, 0x1848) = 0;
    using HashFn = unsigned(__cdecl *)(const char *);
    const HashFn hash = reinterpret_cast<HashFn>(kStringHash);
    for (int e = 0; e < kBindingCount; ++e) {
        char *record = device + kRecordsAt + e * 16;
        field<int>(record, 0) = 1;
        field<unsigned>(record, 4) = hash(*reinterpret_cast<const char *const *>(kBindingTable + e * kEntrySize));
        field<int>(record, 8) = e;
        field<float *>(record, 0xC) = reinterpret_cast<float *>(device + kCurrentAt) + e;
    }
    field<int>(device, 0x1838) = kBindingCount;
}

int add(int source) {
    InterlockedExchange(&g_source, source);
    if (g_device) return g_port;
    char *io = reinterpret_cast<char *>(kIoModule);
    const int index = field<int>(io, 4);
    if (index < 1 || index >= kMaxDevices || !field<void *>(io, 8)) {
        log_line("extra device: the input module is not as expected (%d devices); not added", index);
        return -1;
    }
    using FactoryFn = void *(__cdecl *)(int);
    char *device = static_cast<char *>(reinterpret_cast<FactoryFn>(kFactory)(index));
    if (!device) {
        log_line("extra device: the game could not make a GameDevice");
        return -1;
    }
    set_up_tables(device);
    field<void *>(io, 8 + index * 4) = device;
    field<int>(io, 4) = index + 1;
    field<unsigned>(io, 0x48) |= 1u << index;
    g_device = device;
    g_port = index;
    log_line("extra device: a second GameDevice at port %d, for the second player", index);
    return g_port;
}

void poll(void *device) {
    char *d = static_cast<char *>(device);
    float *current = field<float *>(d, 0x20);
    memcpy(field<float *>(d, 0x1C), current, 0x400);
    float values[kBindingCount] = {};
    const int source = g_source;
    if (source >= 0) pad_sets::actions_of_slot(source, values);
    else if (source == nfsmw::controller_api::kSourceKeyboard) pad_sets::actions_of_keyboard(values);
    for (int e = 0; e < kBindingCount; ++e) current[e] = values[e];
}

}  // namespace pad::extra_device

extern "C" __declspec(dllexport) int mets_controller_support_extra_device(int source) {
    return pad::extra_device::add(source);
}

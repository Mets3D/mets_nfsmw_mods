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

struct Slot {
    void *device = nullptr;
    int port = -1;
    volatile LONG source = nfsmw::controller_api::kSourceNone;
    volatile LONG test = 0;  // an automated test drives the device: gas and steering, in thousandths (see mets_controller_support_test_input)
    volatile LONG test_gas = 0, test_steer = 0;
    volatile LONG test_pause = 0;  // the device says "pause request" (entry 18)
};
Slot g_slots[kSlots];

// The device of the second player is slot 0.
void *&g_device = g_slots[0].device;
int &g_port = g_slots[0].port;
volatile LONG &g_source = g_slots[0].source;

int port() { return g_port; }
int port_for_player(int joystick_port) {
    return joystick_port == -1 && g_device && g_source != nfsmw::controller_api::kSourceNone ? g_port : joystick_port;
}
bool is_extra(void *device) {
    for (const Slot &slot : g_slots)
        if (slot.device && slot.device == device) return true;
    return false;
}
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

int add_slot(int slot_index, int source) {
    if (slot_index < 0 || slot_index >= kSlots) return -1;
    Slot &slot = g_slots[slot_index];
    InterlockedExchange(&slot.source, source);
    if (slot.device) return slot.port;
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
    slot.device = device;
    slot.port = index;
    log_line("extra device: a GameDevice at port %d, for player %d", index, slot_index + 2);
    return slot.port;
}

int add(int source) { return add_slot(0, source); }

void poll(void *device) {
    const Slot *owner = nullptr;
    for (const Slot &slot : g_slots)
        if (slot.device && slot.device == device) owner = &slot;
    if (!owner) return;
    char *d = static_cast<char *>(device);
    float *current = field<float *>(d, 0x20);
    memcpy(field<float *>(d, 0x1C), current, 0x400);
    float values[kBindingCount] = {};
    const int source = owner->source;
    if (source >= 0) pad_sets::actions_of_slot(source, values);
    else if (source == nfsmw::controller_api::kSourceKeyboard) pad_sets::actions_of_keyboard(values);
    if (owner->test) {
        values[0] = owner->test_gas / 1000.0f;                                       // GAS
        values[2] = owner->test_steer < 0 ? owner->test_steer / -1000.0f : 0.0f;    // STEERLEFT
        values[3] = owner->test_steer > 0 ? owner->test_steer / 1000.0f : 0.0f;     // STEERRIGHT
    }
    if (owner->test_pause) values[18] = 1.0f;  // HUD PAUSEREQUEST
    for (int e = 0; e < kBindingCount; ++e) current[e] = values[e];
}

void test_pause(int slot_index, bool down) {
    if (slot_index >= 0 && slot_index < kSlots) g_slots[slot_index].test_pause = down ? 1 : 0;
}

void test_input(int slot_index, bool on, int gas, int steer) {
    if (slot_index < 0 || slot_index >= kSlots) return;
    Slot &slot = g_slots[slot_index];
    slot.test_gas = gas;
    slot.test_steer = steer;
    slot.test = on ? 1 : 0;
}

}  // namespace pad::extra_device

extern "C" __declspec(dllexport) int mets_controller_support_extra_device(int source) {
    return pad::extra_device::add(source);
}

// For automated tests (dev_tools): the device of a slot says gas and steering (thousandths, steering -1000 left to 1000 right) whatever its source is, until
// `on` is 0.
extern "C" __declspec(dllexport) void mets_controller_support_test_input(int slot, int on, int gas, int steer) {
    pad::extra_device::test_input(slot, on != 0, gas, steer);
}

extern "C" __declspec(dllexport) void mets_controller_support_test_pause(int slot, int down) {
    pad::extra_device::test_pause(slot, down != 0);
}

extern "C" __declspec(dllexport) int mets_controller_support_extra_device_slot(int slot, int source) {
    return pad::extra_device::add_slot(slot, source);
}

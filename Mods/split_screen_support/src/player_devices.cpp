#include "player_devices.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <algorithm>

#include "binding_table.h"
#include "controller_api.h"
#include "keyboard2.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::player_devices {

namespace {

namespace api = nfsmw::controller_api;
namespace xinput = nfsmw::xinput;
using namespace nfsmw::binding_table;
using nfsmw::field;

constexpr uintptr_t kIoModule = 0x920DE0;  // +4 device count, +8 devices (16 pointers), +0x48 bit mask of the devices
constexpr int kMaxDevices = 16;
constexpr uintptr_t kFactory = 0x63CD10;     // makes a GameDevice: cdecl, (port)
constexpr uintptr_t kStringHash = 0x5CC240;  // the game's name hash: cdecl, (const char *)
constexpr uintptr_t kPollDevice = 0x6349B0;  // GameDevice::PollDevice: slot 4 of its virtual table, and only called through it
constexpr int kPollSlot = 4, kVTableSlots = 32;
constexpr int kRecordsAt = 0x34, kPreviousAt = 0x1034, kCurrentAt = 0x1434;  // inside a GameDevice, 0x400 bytes of floats
constexpr int kNoDevice = -2;
constexpr int kPauseEntry = 18;

struct PlayerDevice {
    void *device = nullptr;  // their GameDevice
    int port = -1;
    volatile LONG source = kNoDevice;  // the device they play with
    volatile LONG test = 0, test_gas = 0, test_steer = 0, test_pause = 0;
    volatile LONG test_entry[kCount];
    PlayerDevice() {
        for (volatile LONG &entry : test_entry) entry = -1;
    }
};
PlayerDevice g_players[kSlots];
volatile LONG g_first = kNoDevice;  // player 1's device; none: every device drives their vehicle

bool is_device(int device) { return device >= 0 || device == players::kKeyboard || device == players::kKeyboard2; }

// mets_controller_support is told who has which device: its rumble and camera look are per player.
void tell(int player, int source, int port) {
    static const api::PlayerDeviceFn player_device = api::find_player_device();
    if (!player_device) return;
    const int theirs = source >= 0 ? source : source == players::kKeyboard ? api::kSourceKeyboard : player == 0 && source == kNoDevice ? api::kDefault : api::kSourceNone;
    player_device(player, theirs, port);
}

// ---- what a device asks for, per binding entry ----------------------------------------------------------------------------------------------

// The entries the game's input mapping turns into the vehicle, its camera and its HUD actions: 0-13 (gas ... HUD pad), 15-18 (camera change / look back /
// pull back, pause request), mets_controller_support's look entries 25-28 and nfsmw_camera_settings' Previous Camera View. The rest (14, 19-24, 29 and up)
// are the menus' and hotkeys', and come from every device.
bool is_vehicle_entry(int e) { return e <= 13 || (e >= 15 && e <= 18) || (e >= 25 && e <= 28) || e == kPreviousCameraEntry; }

bool keyboard2_plays() {
    if (g_first == players::kKeyboard2) return true;
    for (const PlayerDevice &player : g_players)
        if (player.device && player.source == players::kKeyboard2) return true;
    return false;
}

// The keyboard: the game's key bindings (slots 0 and 1 of the binding table). While Keyboard 2 plays, the keys it uses are its own: the keyboard's bindings
// of them do not drive, and a driving entry left without a key takes W / S / A / D.
void keyboard_actions(const unsigned char *keys, float *values) {
    constexpr int kFallback[] = {0x11, 0x1F, 0x1E, 0x20};  // W, S, A, D for gas, brake, left and right
    const auto down = [keys](int key) { return key > 0 && key < 256 && (keys[key] & 0x80) != 0; };
    const bool shared = keyboard2_plays();
    for (int e = 0; e < kCount; ++e) {
        bool reserved = false, available = false;
        for (int s = 0; s < 2; ++s) {
            const Slot slot = slots_of(kLive, e)[s];
            if (slot.type != kKey) continue;
            if (shared && is_vehicle_entry(e) && keyboard2::uses_key(slot.device)) {
                reserved = true;
                continue;
            }
            available = true;
            if (down(slot.device)) values[e] = 1.0f;
        }
        if (reserved && !available && e < 4 && !keyboard2::uses_key(kFallback[e]) && down(kFallback[e])) values[e] = 1.0f;
    }
}

// A controller without mets_controller_support: the game's bindings cannot be read for one controller alone, so it drives with a fixed layout (that
// mod's default one).
void fixed_layout(int slot, float *values) {
    xinput::Gamepad pad;
    if (!xinput::read_slot(slot, &pad)) return;
    const auto button = [&pad](WORD mask) { return pad.buttons & mask ? 1.0f : 0.0f; };
    const float steer = std::clamp(pad.lx / 32767.0f, -1.0f, 1.0f);
    values[0] = pad.right_trigger / 255.0f;            // gas
    values[1] = pad.left_trigger / 255.0f;             // brake
    values[2] = std::max(-steer, 0.0f);
    values[3] = std::max(steer, 0.0f);
    values[4] = button(xinput::kLeftShoulder);         // handbrake
    values[5] = button(xinput::kButtonX);              // speedbreaker
    values[6] = button(xinput::kButtonA);              // nitrous
    values[7] = button(xinput::kDpadDown);             // shift down
    values[8] = button(xinput::kRightShoulder);        // shift up
    values[9] = button(xinput::kRightThumb);           // reset car
    values[15] = button(xinput::kDpadRight);           // next camera
    values[16] = button(xinput::kButtonB);             // look back
    values[kPauseEntry] = button(xinput::kStart);
    values[kPreviousCameraEntry] = button(xinput::kDpadLeft);
}

// `values` (all 0 before) get what the device asks for.
void actions_of(int device, float *values) {
    if (device >= 0) {
        static const api::ActionsFn actions = api::find_actions();
        if (actions) actions(device, values);
        else fixed_layout(device, values);
        return;
    }
    unsigned char keys[256];
    if (!read_keyboard(keys)) return;
    if (device == players::kKeyboard2) keyboard2::actions(keys, values);
    else keyboard_actions(keys, values);
}

// ---- the game input devices -------------------------------------------------------------------------------------------------------------------
using PollFn = void(__fastcall *)(void *, void *);
PollFn g_game_poll = nullptr;
void *g_first_vtable[kVTableSlots], *g_extra_vtable[kVTableSlots];

// The port 0 device: what the game makes of every device's bindings (with what mets_controller_support adds to it), then player 1's vehicle from their
// device alone.
void __fastcall poll_first(void *device, void *edx) {
    g_game_poll(device, edx);
    const int source = g_first;
    float *current = field<float *>(device, 0x20);
    if (source == kNoDevice || !current) return;
    float own[kCount] = {};
    actions_of(source, own);
    for (int e = 0; e < kCount; ++e)
        if (is_vehicle_entry(e)) current[e] = own[e];
}

// A player's own device: what their device asks for.
void __fastcall poll_extra(void *device, void *edx) {
    g_game_poll(device, edx);
    const PlayerDevice *owner = nullptr;
    for (const PlayerDevice &player : g_players)
        if (player.device == device) owner = &player;
    if (!owner) return;
    float *current = field<float *>(device, 0x20);
    memcpy(field<float *>(device, 0x1C), current, 0x400);
    float values[kCount] = {};
    if (owner->source != kNoDevice) actions_of(owner->source, values);
    if (owner->test) {
        values[0] = owner->test_gas / 1000.0f;
        values[2] = owner->test_steer < 0 ? owner->test_steer / -1000.0f : 0.0f;
        values[3] = owner->test_steer > 0 ? owner->test_steer / 1000.0f : 0.0f;
    }
    if (owner->test_pause) values[kPauseEntry] = 1.0f;
    for (int e = 0; e < kCount; ++e)
        if (owner->test_entry[e] >= 0) values[e] = owner->test_entry[e] / 1000.0f;
    for (int e = 0; e < kCount; ++e) current[e] = values[e];
}

// Gives one device a copy of its virtual table with our poll. The function itself is left alone: mets_controller_support hooks it, and two mods cannot.
bool wrap(void *device, void **copy, PollFn poll) {
    void **vtable = field<void **>(device, 0);
    if (vtable == copy) return true;
    if (vtable[kPollSlot] != reinterpret_cast<void *>(kPollDevice)) {
        g_log.line("player devices: the device at %p is not a GameDevice as expected; left alone", device);
        return false;
    }
    memcpy(copy, vtable, kVTableSlots * sizeof(void *));
    g_game_poll = reinterpret_cast<PollFn>(vtable[kPollSlot]);
    copy[kPollSlot] = reinterpret_cast<void *>(poll);
    field<void **>(device, 0) = copy;
    return true;
}

// The game makes its first GameDevice with all its tables; a later one only gets the fields (the constructor skips them), and the port-0-only
// initialisation that links an input to a value (GameDevice vtable 3) is not run for it. Both are done here the way the game does them for port 0:
// 76 inputs, one per binding entry, each pointing at its value.
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
    const auto hash = reinterpret_cast<unsigned(__cdecl *)(const char *)>(kStringHash);
    for (int e = 0; e < kCount; ++e) {
        char *record = device + kRecordsAt + e * 16;
        field<int>(record, 0) = 1;
        field<unsigned>(record, 4) = hash(name_of(kLive, e));
        field<int>(record, 8) = e;
        field<float *>(record, 0xC) = reinterpret_cast<float *>(device + kCurrentAt) + e;
    }
    field<int>(device, 0x1838) = kCount;
}

// A GameDevice added to the input module's array, at the next port.
void make(PlayerDevice &player, int slot_index) {
    char *io = reinterpret_cast<char *>(kIoModule);
    const int index = field<int>(io, 4);
    if (index < 1 || index >= kMaxDevices || !field<void *>(io, 8)) {
        g_log.line("player devices: the input module is not as expected (%d devices); player %d has no input", index, slot_index + 2);
        return;
    }
    char *device = static_cast<char *>(reinterpret_cast<void *(__cdecl *)(int)>(kFactory)(index));
    if (!device) {
        g_log.line("player devices: the game could not make a GameDevice; player %d has no input", slot_index + 2);
        return;
    }
    set_up_tables(device);
    if (!wrap(device, g_extra_vtable, &poll_extra)) return;
    field<void *>(io, 8 + index * 4) = device;
    field<int>(io, 4) = index + 1;
    field<unsigned>(io, 0x48) |= 1u << index;
    player.device = device;
    player.port = index;
    g_log.line("player devices: a GameDevice at port %d, for player %d", index, slot_index + 2);
}

}  // namespace

void first_player(int device) {
    int source = is_device(device) ? device : kNoDevice;
    void *first = field<void *>(reinterpret_cast<void *>(kIoModule), 8);
    if (source != kNoDevice && (!first || !wrap(first, g_first_vtable, &poll_first))) source = kNoDevice;
    if (InterlockedExchange(&g_first, source) == source) return;
    g_log.line("player 1's vehicle: %s", source == kNoDevice ? "every device" : players::describe(source));
    tell(0, source, 0);
}

int give(int slot, int device) {
    if (slot < 0 || slot >= kSlots) return -1;
    PlayerDevice &player = g_players[slot];
    const int source = is_device(device) ? device : kNoDevice;
    InterlockedExchange(&player.source, source);
    if (!player.device && source != kNoDevice) make(player, slot);
    tell(slot + 1, source, player.port);
    return port(slot);
}

int port(int slot) {
    if (slot < 0 || slot >= kSlots) return -1;
    return g_players[slot].device && g_players[slot].source != kNoDevice ? g_players[slot].port : -1;
}

// A DirectInput keyboard of our own, in front only (it reads nothing while another window has the focus). Enter and numpad Enter are two keys to it,
// which they are not to GetAsyncKeyState.
bool read_keyboard(unsigned char *out) {
    static IDirectInputDevice8A *keyboard = nullptr;
    static bool failed = false;
    const HWND window = *reinterpret_cast<HWND *>(0x982BF4);  // the game's window
    if (!keyboard && !failed && window) {
        IDirectInput8A *direct_input = nullptr;
        if (SUCCEEDED(DirectInput8Create(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A, reinterpret_cast<void **>(&direct_input), nullptr)) && direct_input) {
            if (SUCCEEDED(direct_input->CreateDevice(GUID_SysKeyboard, &keyboard, nullptr)) && keyboard &&
                (FAILED(keyboard->SetDataFormat(&c_dfDIKeyboard)) || FAILED(keyboard->SetCooperativeLevel(window, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE)))) {
                keyboard->Release();
                keyboard = nullptr;
            }
            direct_input->Release();
        }
        failed = !keyboard;
        g_log.line(keyboard ? "the keyboard can be read" : "the keyboard cannot be read (DirectInput): no player can use it");
    }
    if (!keyboard) return false;
    if (SUCCEEDED(keyboard->GetDeviceState(256, out))) return true;
    return SUCCEEDED(keyboard->Acquire()) && SUCCEEDED(keyboard->GetDeviceState(256, out));
}

void test_input(int slot, bool on, int gas, int steer) {
    if (slot < 0 || slot >= kSlots) return;
    g_players[slot].test_gas = gas;
    g_players[slot].test_steer = steer;
    g_players[slot].test = on ? 1 : 0;
}

void test_entry(int slot, int entry, int milli) {
    if (slot >= 0 && slot < kSlots && entry >= 0 && entry < kCount) g_players[slot].test_entry[entry] = milli;
}

void test_pause(int slot, bool down) {
    if (slot >= 0 && slot < kSlots) g_players[slot].test_pause = down ? 1 : 0;
}

}  // namespace split_screen_support::player_devices

// For automated tests (dev_tools, common/split_screen_api.h).
extern "C" __declspec(dllexport) void nfsmw_split_screen_support_test_input(int slot, int on, int gas, int steer) {
    split_screen_support::player_devices::test_input(slot, on != 0, gas, steer);
}
extern "C" __declspec(dllexport) void nfsmw_split_screen_support_test_entry(int slot, int entry, int milli) {
    split_screen_support::player_devices::test_entry(slot, entry, milli);
}
extern "C" __declspec(dllexport) void nfsmw_split_screen_support_test_pause(int slot, int down) {
    split_screen_support::player_devices::test_pause(slot, down != 0);
}

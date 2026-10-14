#include "pad_sets.h"

#include "bindings.h"
#include "controller_api.h"
#include "hooks.h"
#include "input.h"
#include "controls_rumble.h"
#include "rumble.h"

namespace pad::pad_sets {

using namespace pad::bindings;

constexpr int kMaxPads = 8;
constexpr uintptr_t kJoysticks = 0x91F150;  // the game's joystick table: up to 8 pointers
constexpr uintptr_t kBindingValue = 0x628940;  // BindingValue(entry, slot), stdcall, the value in st0
constexpr int kControllerSlot = 2;             // slots 2 and 3 of an entry are the controller's

struct Set {
    Slot slots[kBindingCount][2];
    bool valid = false;
};
Set g_sets[kMaxPads];  // each controller's set, except the one in the live table (index 0 holds controller 0's meanwhile)
int g_live = 0;        // whose set the live table holds
int g_menu_pad = 0;    // the controller the Controls screen shows

void *joystick(int index) { return index >= 0 && index < kMaxPads ? *reinterpret_cast<void **>(kJoysticks + index * 4) : nullptr; }

int joystick_count() {
    int n = 0;
    while (n < kMaxPads && joystick(n)) ++n;
    return n;
}

void *joystick_di_device(int index) {
    void *object = joystick(index);
    return object ? field<void *>(object, 0x468) : nullptr;
}

int joystick_of_device(void *di_device) {
    for (int i = 0; i < kMaxPads && joystick(i); ++i)
        if (joystick_di_device(i) == di_device) return i;
    return -1;
}

// ---- the sets -------------------------------------------------------------------------------------------------------------
void copy_out(Set &set, int pad) {  // live table -> set (a new binding says controller 0: fix that)
    for (int e = 0; e < kBindingCount; ++e)
        for (int s = 0; s < 2; ++s) {
            Slot slot = slots_of(kBindingTable, e)[kControllerSlot + s];
            if (slot.type >= kButton) slot.device = pad;
            set.slots[e][s] = slot;
        }
    set.valid = true;
}

void copy_in(const Set &set) {
    for (int e = 0; e < kBindingCount; ++e)
        for (int s = 0; s < 2; ++s) slots_of(kBindingTable, e)[kControllerSlot + s] = set.slots[e][s];
}

void save_ini(int pad) {
    for (int e = 0; e < kBindingCount; ++e) {
        char key[16], text[64];
        snprintf(key, sizeof(key), "%d_%d", pad, e);
        const Slot *s = g_sets[pad].slots[e];
        snprintf(text, sizeof(text), "%d %d %d %d", s[0].type, s[0].object, s[1].type, s[1].object);
        WritePrivateProfileStringA("pad_bindings", key, text, g_ini);
    }
    log_line("controller %d: bindings saved", pad + 1);
}

void load_settings() {
    for (int pad = 1; pad < kMaxPads; ++pad)
        for (int e = 0; e < kBindingCount; ++e) {
            char key[16], text[64];
            snprintf(key, sizeof(key), "%d_%d", pad, e);
            GetPrivateProfileStringA("pad_bindings", key, "", text, sizeof(text), g_ini);
            int t0, o0, t1, o1;
            if (sscanf(text, "%d %d %d %d", &t0, &o0, &t1, &o1) != 4) continue;
            g_sets[pad].slots[e][0] = {t0, pad, o0};
            g_sets[pad].slots[e][1] = {t1, pad, o1};
            g_sets[pad].valid = true;
        }
}

// Puts a controller's set in the live table (the one shown by the Controls screen), keeping the one that was there.
void select(int pad) {
    if (pad == g_live) return;
    Set before = g_sets[g_live];
    copy_out(g_sets[g_live], g_live);
    if (g_live > 0 && memcmp(&before, &g_sets[g_live], sizeof(Set)) != 0) save_ini(g_live);
    if (!g_sets[pad].valid) {  // never edited: starts as a copy of controller 0's
        g_sets[pad] = g_sets[0];
        for (auto &entry : g_sets[pad].slots)
            for (Slot &slot : entry)
                if (slot.type >= kButton) slot.device = pad;
    }
    copy_in(g_sets[pad]);
    g_live = pad;
    bindings::resync_changes();  // the swap is not the player's editing: no log lines, no look bindings saved
}

int live_pad() { return g_live; }

// The entries the game's input mapping turns into the vehicle, its camera and its HUD actions: 0-13 (gas ... HUD pad), 15-18
// (camera change / look back / pull back, pause request), the look entries 25-28 and Previous Camera View (nfsmw_camera_settings'
// entry). The rest (14, 19-24, 29 and up) are the menus' and hotkeys', and come from every device.
bool is_vehicle_entry(int e) {
    return e <= 13 || (e >= 15 && e <= 18) || (e >= 25 && e <= 28) || e == nfsmw::binding_table::kPreviousCameraEntry;
}

void restrict_vehicle_entries(float *values) {
    const int source = input::vehicle_source();
    if (source == nfsmw::controller_api::kDefault) return;
    float own[kBindingCount] = {};
    if (source == nfsmw::controller_api::kSourceKeyboard) actions_of_keyboard(own);
    else actions_of_slot(source, own);
    for (int e = 0; e < kBindingCount; ++e)
        if (is_vehicle_entry(e)) values[e] = own[e];
}

bool actions_of_keyboard(float *values) {
    for (int e = 0; e < kBindingCount; ++e) {
        values[e] = 0.0f;
        for (int s = 0; s < 2; ++s) {
            const Slot slot = slots_of(kBindingTable, e)[s];
            if (slot.type == kKey && input::key_down(slot.device)) values[e] = 1.0f;
        }
    }
    return true;
}

// The entries' values that one XInput controller asks for through the set of the game controller that reports it (for
// players the game does not poll: the second one of a split screen).
// Whether the game has no more than one XInput game controller (then the pads cannot be told apart and share its set).
bool no_xinput_slot_resolved() {
    int xinput_controllers = 0;
    for (int k = 0, n = joystick_count(); k < n; ++k)
        if (input::xinput_slot_of_joystick(k) >= 0) ++xinput_controllers;
    return xinput_controllers == 0;
}

bool actions_of_slot(int xinput_slot, float *values) {
    const int n = joystick_count();
    int pad = -1;
    for (int k = 0; k < n && pad < 0; ++k)
        if (input::xinput_slot_of_joystick(k) == xinput_slot) pad = k;
    if (pad < 0 && n >= 1 && no_xinput_slot_resolved()) pad = 0;  // one game controller: all pads share it
    if (pad < 0) return false;
    xinput::Gamepad state;
    if (!xinput::read_slot(xinput_slot, &state)) return false;
    for (int e = 0; e < kBindingCount; ++e) {
        values[e] = 0.0f;
        for (int s = 0; s < 2; ++s) {
            Slot slot;
            if (pad == g_live) slot = slots_of(kBindingTable, e)[kControllerSlot + s];
            else if (g_sets[pad].valid) slot = g_sets[pad].slots[e][s];
            else if (g_live == 0) slot = slots_of(kBindingTable, e)[kControllerSlot + s];
            else slot = g_sets[0].slots[e][s];
            if (slot.type < kButton) continue;
            const float v = input::binding_value(slot.type, slot.object, state);
            if (v > values[e]) values[e] = v;
        }
    }
    return true;
}

// ---- what the game gets ---------------------------------------------------------------------------------------------------
// The game polled the live set (against the controllers it names); every other controller's set is read the same way, by
// putting its two slots in the entry for a moment, and the strongest value of an entry wins.
int g_source[kBindingCount];  // whose set made each entry's value (0 = the live set, n = controller n)

void log_active(const float *values) {
    static unsigned long long last[2] = {};
    static int lines = 0;
    unsigned long long mask[2] = {};
    for (int e = 0; e < kBindingCount; ++e)
        if (values[e] > 0.3f) mask[e / 64] |= 1ull << (e % 64);
    if (mask[0] == last[0] && mask[1] == last[1]) return;
    last[0] = mask[0];
    last[1] = mask[1];
    if (lines++ >= 400) return;
    char text[400];
    int n = snprintf(text, sizeof(text), "inputs:");
    for (int e = 0; e < kBindingCount && n < 330; ++e)
        if (mask[e / 64] >> (e % 64) & 1) {
            const char *name = *reinterpret_cast<const char *const *>(kBindingTable + e * kEntrySize);
            n += snprintf(text + n, sizeof(text) - n, " %s(%d)=%.1f<-%s%d", name, e, values[e], g_source[e] ? "controller " : "the live set", g_source[e]);
        }
    log_line("%s", text);
}

void add_values(float *values) {
    const int n = joystick_count();
    for (int e = 0; e < kBindingCount; ++e) g_source[e] = 0;
    if (n >= 2) {
    using ValueFn = float(__stdcall *)(int, int);
    const ValueFn value = reinterpret_cast<ValueFn>(kBindingValue);
    for (int pad = 0; pad < n; ++pad) {
        if (pad == g_live) continue;
        const Set *set = g_sets[pad].valid ? &g_sets[pad] : nullptr;
        const Set *base = g_live == 0 ? nullptr : &g_sets[0];  // controller 0's set, when it is not the live one
        for (int e = 0; e < kBindingCount; ++e) {
            Slot *live = slots_of(kBindingTable, e) + kControllerSlot;
            const Slot saved[2] = {live[0], live[1]};
            for (int s = 0; s < 2; ++s) {
                Slot slot = set ? set->slots[e][s] : base ? base->slots[e][s] : saved[s];
                if (slot.type < kButton) continue;
                slot.device = pad;
                live[0] = live[1] = {kNone, 0, 0};
                live[s] = slot;
                const float v = value(e, kControllerSlot + s);
                if (v > values[e]) {
                    values[e] = v;
                    g_source[e] = pad + 1;
                }
            }
            live[0] = saved[0];
            live[1] = saved[1];
        }
    }
    }
    if (g_cfg.log_inputs) log_active(values);
}

// ---- the Controls screen --------------------------------------------------------------------------------------------------
using RefreshFn = void(FASTCALL *)(void *, void *);
using DeleteFn = void *(FASTCALL *)(void *, void *, int);
using NameFn = const char *(__stdcall *)(int);
DeleteFn g_orig_delete;
NameFn g_orig_name;

// The device switch is the screen's two shoulder buttons (PAD_LTRIGGER / PAD_RTRIGGER), which both end in the same
// function; the screen's message handler tells which one it was.
int g_direction = 1;  // 1 next, -1 previous
using MessageFn = void(FASTCALL *)(void *, void *, unsigned, void *, unsigned, unsigned);
MessageFn g_orig_message;
void FASTCALL hook_message(void *screen, void *edx, unsigned message, void *object, unsigned a, unsigned b) {
    message = controls_rumble::filter_message(screen, message);
    if (message == 0x5073EF13) g_direction = -1;      // PAD_LTRIGGER
    else if (message == 0xD9FEEC59) g_direction = 1;  // PAD_RTRIGGER
    g_orig_message(screen, edx, message, object, a, b);
}

int menu_pad() { return g_menu_pad; }

void show_device(void *screen, int new_column, int pad) {
    select(new_column == 3 ? pad : 0);
    g_menu_pad = new_column == 3 ? pad : 0;
    field<int>(screen, 0xE4) = new_column;
    reinterpret_cast<RefreshFn>(0x52AC70)(screen, nullptr);  // the rows, and the header
    void **table = *reinterpret_cast<void ***>(static_cast<char *>(screen) + 0x2C);
    reinterpret_cast<RefreshFn>(table[2])(static_cast<char *>(screen) + 0x2C, nullptr);
    if (new_column == 3) {
        const int slot = input::xinput_slot_of_joystick(pad);
        log_line("Controls menu: controller %d of %d (XInput controller %d)", pad + 1, joystick_count(), slot + 1);
    } else {
        log_line("Controls menu: keyboard");
    }
}

// The device switch: keyboard, controller 1, controller 2 ... in a circle, in the direction of the button.
void FASTCALL hook_switch_device(void *screen, void *edx) {
    (void)edx;
    int &column = field<int>(screen, 0xE4);  // 1 keyboard, 3 controller
    const int n = joystick_count();
    if (n == 0) return;
    if (controls_rumble::on_device_switch(screen)) return;  // the Rumble hint: the page opens instead
    const int item = column == 3 ? g_menu_pad + 1 : 0;  // 0 the keyboard, 1.. the controllers
    const int next = ((item + g_direction) % (n + 1) + n + 1) % (n + 1);
    const int new_column = next == 0 ? 1 : 3, pad = next == 0 ? 0 : next - 1;
    if (new_column == column && pad == g_menu_pad) return;
    show_device(screen, new_column, pad);
    if (new_column == 3 && n > 1) rumble::pulse(input::xinput_slot_of_joystick(pad));
}

// The screen closes: controller 0's set goes back in the live table (the profile saves that one).
void *FASTCALL hook_delete_screen(void *screen, void *edx, int flags) {
    select(0);
    g_menu_pad = 0;
    return g_orig_delete(screen, edx, flags);
}

// The header's device name: tells the controllers apart.
const char *__stdcall hook_device_name(int column) {
    const char *name = g_orig_name(column);
    if (column != 3 || joystick_count() < 2) return name;
    static char text[0x140];
    snprintf(text, sizeof(text), "%.250s #%d", name, g_menu_pad + 1);
    return text;
}

void install_hooks() {
    const nfsmw::Hook hooks[] = {
        {"switch", "Controls menu device switch", 0x52AF20, {0x56, 0x8B, 0xF1, 0xE8, 0x48, 0x94, 0x10}, false,
         nfsmw::detour(&hook_switch_device), nullptr},
        {"message", "Controls menu messages", 0x545F70, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B}, false,
         nfsmw::detour(&hook_message), nfsmw::orig(&g_orig_message)},
        {"close", "Controls menu close", 0x545F50, {0x56, 0x8B, 0xF1, 0xE8, 0x28, 0x48, 0xFE}, false,
         nfsmw::detour(&hook_delete_screen), nfsmw::orig(&g_orig_delete)},
        {"name", "device name", 0x628C60, {0x8B, 0x44, 0x24, 0x04, 0x48, 0x74, 0x0C}, false,
         nfsmw::detour(&hook_device_name), nfsmw::orig(&g_orig_name)},
    };
    nfsmw::install_hooks(hooks, 4, g_log, nullptr, "controller list");
}

}  // namespace pad::pad_sets

extern "C" __declspec(dllexport) int nfsmw_controller_support_live_pad() { return pad::pad_sets::live_pad(); }

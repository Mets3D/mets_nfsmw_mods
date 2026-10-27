#include "pad_sets.h"

#include "bindings.h"
#include "controller_api.h"
#include "hooks.h"
#include "input.h"
#include "confirm_changes.h"
#include "controls_options.h"
#include "device_settings.h"
#include "profile.h"
#include "rumble.h"
#include "steam_pads.h"

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
char g_profile[MAX_PATH] = "?";   // the profile file the sets were read from ("": none, "?": not yet)
char g_keys[kMaxPads][48] = {};   // the controller each set belongs to (see controller_key); empty: no controller there

void *joystick(int index) { return index >= 0 && index < kMaxPads ? *reinterpret_cast<void **>(kJoysticks + index * 4) : nullptr; }

int joystick_count() {
    int n = 0;
    while (n < kMaxPads && joystick(n)) ++n;
    return n;
}

void *joystick_object(int index) { return joystick(index); }

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

// ---- the profile's controller_settings.ini ---------------------------------------------------------------------------------
// Controller 0's set is the profile's (the game saves the live table with it). The others are kept in [pad_bindings] of the profile's
// controller_settings.ini (profile.h), under keys <controller>_<binding>: the controller is named as in device_settings.cpp (its vendor,
// product and serial when those tell it from others, else its XInput slot), so a set follows its controller, not its place in the
// game's list. A set that was never edited is not written (the controller then uses controller 0's).
void controller_key(int pad, char *out, size_t size) {
    const int slot = input::xinput_slot_of_joystick(pad);
    if (slot >= 0) snprintf(out, size, "%s", device_settings::key_of(slot));
    else snprintf(out, size, "joystick%d", pad + 1);
}

void save_ini(int pad) {
    for (int e = 0; e < kBindingCount; ++e) {
        char key[64], text[64];
        snprintf(key, sizeof(key), "%s_%d", g_keys[pad], e);
        const Slot *s = g_sets[pad].slots[e];
        snprintf(text, sizeof(text), "%d %d %d %d", s[0].type, s[0].object, s[1].type, s[1].object);
        profile::set("pad_bindings", key, text);
    }
    log_line("controller %d (%s): bindings saved to %s", pad + 1, g_keys[pad], profile::file()[0] ? profile::file() : "the mod's ini");
}

void load_pad(int pad) {
    g_sets[pad] = Set();
    if (pad == 0 || !g_keys[pad][0]) return;
    for (int e = 0; e < kBindingCount; ++e) {
        char key[64], text[64] = "";
        snprintf(key, sizeof(key), "%s_%d", g_keys[pad], e);
        profile::get_string("pad_bindings", key, text, sizeof(text));
        int t0, o0, t1, o1;
        if (sscanf(text, "%d %d %d %d", &t0, &o0, &t1, &o1) != 4) continue;
        g_sets[pad].slots[e][0] = {t0, pad, o0};
        g_sets[pad].slots[e][1] = {t1, pad, o1};
        g_sets[pad].valid = true;
    }
}

// Reads the sets of the controllers again when the profile or the controller of a place has changed (the game's list can grow, shrink or
// come in another order, and another profile can be loaded). Not while the Controls screen has another controller's set in the live
// table: that one may hold changes not written yet. At most every half second, unless `force`.
void refresh_sets(bool force = false) {
    if (g_live != 0) return;
    static DWORD next = 0;
    const DWORD now = GetTickCount();
    if (!force && static_cast<LONG>(now - next) < 0) return;
    next = now + 500;
    const bool new_profile = strcmp(profile::file(), g_profile) != 0;
    if (new_profile) {
        snprintf(g_profile, sizeof(g_profile), "%s", profile::file());
        log_line("the bindings of the extra controllers are read from %s", g_profile[0] ? g_profile : "the mod's ini (the profile has no folder yet)");
    }
    const int n = joystick_count();
    for (int pad = 1; pad < kMaxPads; ++pad) {
        char key[sizeof(g_keys[0])] = "";
        if (pad < n) controller_key(pad, key, sizeof(key));
        if (!new_profile && !strcmp(key, g_keys[pad])) continue;
        snprintf(g_keys[pad], sizeof(g_keys[pad]), "%s", key);
        load_pad(pad);
        if (key[0]) log_line("controller %d (%s): %s", pad + 1, key, g_sets[pad].valid ? "its own bindings" : "no bindings of its own (controller 1's)");
    }
}

// Puts a controller's set in the live table (the one shown by the Controls screen), keeping the one that was there.
void select(int pad) {
    refresh_sets(true);
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
    refresh_sets();
    const int n = joystick_count();
    int pad = -1;
    for (int k = 0; k < n && pad < 0; ++k)
        if (input::xinput_slot_of_joystick(k) == xinput_slot) pad = k;
    if (pad < 0 && n >= 1 && no_xinput_slot_resolved()) pad = 0;  // one game controller: all pads share it
    // Said once per XInput controller and reason: a second player's controller that does nothing is easier to explain with this.
    static bool said_no_pad[4], said_no_state[4];
    const int said = xinput_slot >= 0 && xinput_slot < 4 ? xinput_slot : 0;
    if (pad < 0) {
        if (!said_no_pad[said]) log_line("XInput controller %d drives nothing of its own: no game controller reports it (%d game controller(s))", xinput_slot + 1, n);
        said_no_pad[said] = true;
        return false;
    }
    said_no_pad[said] = false;
    xinput::Gamepad state;
    if (!xinput::read_slot(xinput_slot, &state)) {
        if (!said_no_state[said]) log_line("XInput controller %d cannot be read (disconnected?)", xinput_slot + 1);
        said_no_state[said] = true;
        return false;
    }
    said_no_state[said] = false;
    for (int e = 0; e < kBindingCount; ++e) {
        values[e] = 0.0f;
        for (int s = 0; s < 2; ++s) {
            Slot slot;
            if (pad == g_live) slot = slots_of(kBindingTable, e)[kControllerSlot + s];
            else if (g_sets[pad].valid) slot = g_sets[pad].slots[e][s];
            else if (g_live == 0) slot = slots_of(kBindingTable, e)[kControllerSlot + s];
            else slot = g_sets[0].slots[e][s];
            if (slot.type < kButton) continue;
            const float v = input::binding_value(slot.type, slot.object, state, xinput_slot);
            if (v > values[e]) values[e] = v;
        }
    }
    return true;
}

// ---- what the game gets ---------------------------------------------------------------------------------------------------
// The game polled the live set (against the controllers it names); every other controller's set is read the same way, by
// putting its two slots in the entry for a moment, and the strongest value of an entry wins.
void add_values(float *values) {
    refresh_sets();
    const int n = joystick_count();
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
                if (v > values[e]) values[e] = v;
            }
            live[0] = saved[0];
            live[1] = saved[1];
        }
    }
    }
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
void *g_screen = nullptr;  // the Controls screen, while it is open
void FASTCALL hook_message(void *screen, void *edx, unsigned message, void *object, unsigned a, unsigned b) {
    g_screen = screen;
    message = controls_options::filter_message(screen, message);
    if (message == 0x5073EF13) g_direction = -1;      // PAD_LTRIGGER
    else if (message == 0xD9FEEC59) g_direction = 1;  // PAD_RTRIGGER
    message = confirm_changes::filter_message(screen, message);
    if (!message) return;
    g_orig_message(screen, edx, message, object, a, b);
}

int menu_pad() { return g_menu_pad; }

// What the Controls screen calls a controller: its real model ("Xbox One S Controller"; "#2" for a second of the same name) or
// DirectInput's name for it, else the game's name, with its number when there are several.
void label_of_pad(int pad, const char *game_name, char *out, size_t size) {
    const int n = joystick_count();
    char name[128];
    input::joystick_name(pad, name, sizeof(name));
    if (!name[0]) {
        if (n < 2) snprintf(out, size, "%.250s", game_name);
        else snprintf(out, size, "%.250s #%d", game_name, pad + 1);
        return;
    }
    int same = 0, ordinal = 0;
    for (int k = 0; k < n; ++k) {
        char other[128];
        input::joystick_name(k, other, sizeof(other));
        if (strcmp(other, name)) continue;
        ++same;
        if (k <= pad) ++ordinal;
    }
    if (same > 1) snprintf(out, size, "%s #%d", name, ordinal);
    else snprintf(out, size, "%s", name);
}

void show_device(void *screen, int new_column, int pad) {
    select(new_column == 3 ? pad : 0);
    g_menu_pad = new_column == 3 ? pad : 0;
    field<int>(screen, 0xE4) = new_column;
    reinterpret_cast<RefreshFn>(0x52AC70)(screen, nullptr);  // the rows, and the header
    void **table = *reinterpret_cast<void ***>(static_cast<char *>(screen) + 0x2C);
    reinterpret_cast<RefreshFn>(table[2])(static_cast<char *>(screen) + 0x2C, nullptr);
    if (new_column == 3) {
        const int slot = input::xinput_slot_of_joystick(pad);
        char label[300];
        label_of_pad(pad, "?", label, sizeof(label));
        log_line("Controls menu: controller %d of %d: %s (XInput controller %d)", pad + 1, joystick_count(), label, slot + 1);
    } else {
        log_line("Controls menu: keyboard");
    }
}

// A controller was added to the game's list: the open Controls screen shows it (the header names the controller by its number).
// The screen hides its device elements (object 0x8A41F5B9: the shoulder button prompts and the Settings hint, with the script HIDE) when it is built
// with no controller; they come back with the first one, by one of the object's own scripts.
void controllers_changed() {
    if (!g_screen) return;
    const char *package = field<const char *>(g_screen, 0x10);
    const auto find_object = reinterpret_cast<void *(*)(const char *, unsigned)>(0x524850);  // FEngFindObject
    const auto find_script = reinterpret_cast<void *(FASTCALL *)(void *, void *, unsigned)>(0x5B1360);  // FEObject::FindScript
    const auto set_script = reinterpret_cast<void (*)(void *, unsigned, bool)>(0x514D10);    // FEngSetScript
    void *object = package ? find_object(package, 0x8A41F5B9) : nullptr;
    if (object && joystick_count() > 0) {
        for (const char *name : {"SHOW", "INIT", "ON", "NORMAL"}) {
            const unsigned hash = nfsmw::bstring_hash(name);
            if (!find_script(object, nullptr, hash)) continue;
            set_script(object, hash, true);
            log_line("Controls menu: the device elements are shown again (script %s)", name);
            break;
        }
    } else {
        log_line("Controls menu: device elements %s", object ? "not touched (no controller)" : "not found");
    }
    show_device(g_screen, field<int>(g_screen, 0xE4), g_menu_pad);
}

// The device switch: keyboard, controller 1, controller 2 ... in a circle, in the direction of the button.
void FASTCALL hook_switch_device(void *screen, void *edx) {
    (void)edx;
    int &column = field<int>(screen, 0xE4);  // 1 keyboard, 3 controller
    const int n = joystick_count();
    if (n == 0) return;
    if (controls_options::on_device_switch(screen)) return;  // the Rumble hint: the page opens instead
    const int item = column == 3 ? g_menu_pad + 1 : 0;  // 0 the keyboard, 1.. the controllers
    const int next = ((item + g_direction) % (n + 1) + n + 1) % (n + 1);
    const int new_column = next == 0 ? 1 : 3, pad = next == 0 ? 0 : next - 1;
    if (new_column == column && pad == g_menu_pad) return;
    show_device(screen, new_column, pad);
    if (new_column == 3 && n > 1) rumble::pulse(input::rumble_slot_of_joystick(pad));
}

// The screen closes: controller 0's set goes back in the live table (the profile saves that one).
void *FASTCALL hook_delete_screen(void *screen, void *edx, int flags) {
    if (screen == g_screen) g_screen = nullptr;
    select(0);
    g_menu_pad = 0;
    return g_orig_delete(screen, edx, flags);
}

// The header's device name: tells the controllers apart.
const char *__stdcall hook_device_name(int column) {
    const char *name = g_orig_name(column);
    if (column != 3) return name;
    static char text[0x140];
    label_of_pad(g_menu_pad, name, text, sizeof(text));
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

// The name of a device as the Controls menu shows it: the keyboard's, or the controller of an XInput slot (its model, "#2" for a second of the same name).
// `source`: an XInput controller 0-3, or controller_api::kSourceKeyboard. Returns 0 when the controller is not known (the label is then "Controller N").
extern "C" __declspec(dllexport) int mets_controller_support_device_label(int source, char *out, int size) {
    using namespace pad::pad_sets;
    if (!out || size < 2) return 0;
    if (source == nfsmw::controller_api::kSourceKeyboard) {
        snprintf(out, size, "%s", g_orig_name ? g_orig_name(1) : "Keyboard");
        return 1;
    }
    for (int pad = 0; pad < joystick_count(); ++pad)
        if (pad::input::xinput_slot_of_joystick(pad) == source) {
            label_of_pad(pad, g_orig_name ? g_orig_name(3) : "Controller", out, size);
            return 1;
        }
    snprintf(out, size, "Controller %d", source + 1);
    return 0;
}

extern "C" __declspec(dllexport) int mets_controller_support_live_pad() { return pad::pad_sets::live_pad(); }

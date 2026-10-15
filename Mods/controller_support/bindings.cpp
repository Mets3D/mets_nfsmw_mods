// The PC binding table: the camera look entries (renamed, bound to the right stick, kept in nfsmw_controller_support.ini) and a log
// of every change the Controls menu makes to the table.
#include <cstdio>

#include "bindings.h"
#include "controls_menu.h"
#include "default_layout.h"
#include "extra_device.h"
#include "pad_sets.h"
#include "hooks.h"
#include "input.h"
#include "menu_buttons.h"

namespace pad::bindings {

float g_look_input[L_COUNT];
bool g_look_entries_ok = false;


// Camera look: four entries no input mapping in the game data refers to (checked against GLOBAL/attributes.bin),
// so their device scalars carry nothing else. Bound to the right stick (rotation axes X/Y, device 0) by default, in
// the default table (used by the Controls menu's Defaults) and the live one. The profile saves only entries 0-18.
const char *const kLookOldName[L_COUNT] = {"SC_A", "SC_D", "SC_W", "SC_S"};
// New names: the Controls menu shows an entry by the language string whose hash is bStringHash(name).
const char *const kLookName[L_COUNT] = {"CAM_ACTION_LOOK_LEFT", "CAM_ACTION_LOOK_RIGHT", "CAM_ACTION_LOOK_UP",
                                        "CAM_ACTION_LOOK_DOWN"};
const char *const kLookText[L_COUNT] = {"Look Left", "Look Right", "Look Up", "Look Down"};
const char *const kLookKey[L_COUNT] = {"left", "right", "up", "down"};  // nfsmw_controller_support.ini [look_bindings]
constexpr Slot kLookDefault[L_COUNT] = {{kRotLow, 0, 0}, {kRotHigh, 0, 0}, {kRotLow, 0, 1}, {kRotHigh, 0, 1}};

bool look_entries_named(uintptr_t table, const char *which) {
    for (int l = 0; l < L_COUNT; ++l) {
        const char *name = name_of(table, kLookEntry[l]);
        if (!name || (strcmp(name, kLookOldName[l]) != 0 && strcmp(name, kLookName[l]) != 0)) {
            log_line("look: %s binding entry %d is \"%s\", not %s; camera look bindings left alone", which,
                     kLookEntry[l], name ? name : "?", kLookOldName[l]);
            return false;
        }
    }
    return true;
}

void set_look_slots(uintptr_t table, int look, const Slot *slots, int first, int count) {
    set_slots(table, kLookEntry[look], slots, first, count);
}

void look_default_slots(int look, Slot out[4]) {
    out[0] = out[1] = out[3] = Slot{};
    out[2] = kLookDefault[look];
}

// The default table (what the Controls menu's Defaults use, and what the live table is built from): renamed, no
// keyboard keys, right stick.
void prepare_default_look_entries() {
    if (!look_entries_named(kDefault, "default")) return;
    for (int l = 0; l < L_COUNT; ++l) {
        set_name(kDefault, kLookEntry[l], kLookName[l]);
        Slot slots[4];
        look_default_slots(l, slots);
        set_slots(kDefault, kLookEntry[l], slots, 0, 4);
    }
    g_look_entries_ok = true;
    log_line("look: default table entries renamed and bound to the right stick");
}

// The live table at the first input poll: renamed, then the bindings saved in the ini (or the defaults).
void load_live_look_entries() {
    if (!look_entries_named(kBindingTable, "live")) {
        g_look_entries_ok = false;
        return;
    }
    for (int l = 0; l < L_COUNT; ++l) {
        set_name(kBindingTable, kLookEntry[l], kLookName[l]);
        char text[128] = {};
        GetPrivateProfileStringA("look_bindings", kLookKey[l], "", text, sizeof(text), g_ini);
        Slot slots[4];
        const bool saved = text[0] && parse_slots(text, slots);
        if (!saved) look_default_slots(l, slots);
        set_look_slots(kBindingTable, l, slots, 0, 4);
        log_line("look: %s from %s", kLookText[l], saved ? "the ini" : "the defaults");
    }
    g_look_entries_ok = true;
}

void save_look_bindings() {
    for (int l = 0; l < L_COUNT; ++l) {
        char text[128];
        format_slots(slots_of(kBindingTable, kLookEntry[l]), text, sizeof(text));
        WritePrivateProfileStringA("look_bindings", kLookKey[l], text, g_ini);
    }
}

// The game's controller defaults clear the controller slots of entries it does not know: the look entries get theirs back.
void restore_look_defaults() {
    if (!g_look_entries_ok) return;
    for (int l = 0; l < L_COUNT; ++l) {
        Slot slots[4];
        look_default_slots(l, slots);
        set_look_slots(kBindingTable, l, slots, 2, 2);
    }
}

// What nfsmw_custom_menus puts on the Controls screen for us (the first time the Options menu opens): the four look rows at
// the end of both lists, and the language string of each row's title (keyed by the entry's name).
void provide_controls_rows(const nfsmw::custom_menus::Host *host) {
    if (!g_look_entries_ok) return;
    static nfsmw::custom_menus::ControlsRow rows[L_COUNT];
    for (int l = 0; l < L_COUNT; ++l) {
        rows[l] = {kLookEntry[l], -1};
        host->register_text(nfsmw::bstring_hash(kLookName[l]), kLookText[l]);
    }
    host->register_controls_rows(rows, L_COUNT);
    host->set_binding_label(&controls_menu::binding_label);
    host->add_controller_defaults(&restore_look_defaults);
}


Slot g_last_slots[kBindingCount][4];
bool g_have_slots = false;
DWORD g_next_binding_check = 0;

void resync_changes() {
    for (int e = 0; e < kBindingCount; ++e)
        memcpy(g_last_slots[e], reinterpret_cast<const unsigned char *>(kBindingTable + e * kEntrySize) + 4, sizeof(g_last_slots[e]));
}

void log_binding_changes() {
    if (GetTickCount() < g_next_binding_check) return;
    g_next_binding_check = GetTickCount() + 1000;
    bool look_changed = false;
    for (int e = 0; e < kBindingCount; ++e) {
        const unsigned char *entry = reinterpret_cast<const unsigned char *>(kBindingTable + e * kEntrySize);
        const Slot *slots = reinterpret_cast<const Slot *>(entry + 4);
        if (g_have_slots && memcmp(slots, g_last_slots[e], sizeof(g_last_slots[e])) == 0) continue;
        char text[160];
        int n = 0;
        for (int s = 0; s < 4; ++s)
            if (slots[s].type)
                n += snprintf(text + n, sizeof(text) - n, " [%d: type %d dev %d obj %d]", s, slots[s].type,
                              slots[s].device, slots[s].object);
        if (!n) snprintf(text, sizeof(text), " (none)");
        // The first pass is the table as it was loaded (all of it): only what changes after that is the user's doing.
        if (g_have_slots) log_line("binding %2d %-24s%s", e, *reinterpret_cast<const char *const *>(entry), text);
        if (g_have_slots)
            for (int l = 0; l < L_COUNT; ++l) look_changed |= kLookEntry[l] == e;
        memcpy(g_last_slots[e], slots, sizeof(g_last_slots[e]));
    }
    g_have_slots = true;
    if (look_changed && g_look_entries_ok && pad_sets::live_pad() == 0) save_look_bindings();
}

MethodFn g_orig_poll;

void FASTCALL hook_poll(void *self, void *edx) {
    g_orig_poll(self, edx);
    menu_buttons::apply_live();
    default_layout::apply_live();
    static bool first = true;
    if (first) {
        first = false;
        input::hook_dinput();
        if (g_look_entries_ok) load_live_look_entries();
    }
    if (extra_device::is_extra(self)) return extra_device::poll(self);  // the second player's device
    if (field<int>(self, 0x24) != 0) return;  // GameDevice port: only port 0 polls
    const float *current = field<float *>(self, 0x20);
    if (current) {
        pad_sets::add_values(const_cast<float *>(current));  // the other controllers' sets
        pad_sets::restrict_vehicle_entries(const_cast<float *>(current));  // the vehicle from player 1's device only
    }
    for (int l = 0; l < L_COUNT && current; ++l) g_look_input[l] = current[kLookEntry[l]];
    log_binding_changes();
}


void install_hooks() {
    const nfsmw::Hook hooks[] = {
        {"poll", "GameDevice::PollDevice", 0x6349B0, {0x83, 0xEC, 0x0C, 0x55, 0x56, 0x8B, 0xF1}, false,
         nfsmw::detour(&hook_poll), nfsmw::orig(&g_orig_poll)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "bindings");
}

}  // namespace pad::bindings

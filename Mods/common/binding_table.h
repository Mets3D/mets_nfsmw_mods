// The game's PC binding table (the entries the Controls screen edits), for the mods that own entries of it. Header only.
//
// The game's PC input device ("GameDevice"; only port 0 polls) has 76 binding entries: each is {name, 4 slots of
// {type, device, object}}; type 1 = keyboard key, 2 = button, 3 = POV, 4/5 = X/Y/Z axis (object 0-2) towards 0 /
// towards 65535, 6/7 = rotation axes, 8/9 = sliders. Slots 0 and 1 are the keyboard's, 2 and 3 the controller's. A slot's
// value comes from BindingValue (0x628940); axes give proportional values. The Controls screen edits slots; the game's action
// mapping reads the entries' values. The live table is at 0x91F418; the default table (what Defaults and the start-up copy)
// at 0x8F6D80. The profile saves only entries 0-18, so a mod's own entries keep their bindings in its own ini.
//
// Entries nothing refers to (no hash hit in GLOBAL/attributes.bin) can be taken over by renaming them: the Controls screen shows
// an entry under the language string keyed bStringHash(name).
#pragma once

#include <cstdio>

#include "nfsmw_util.h"

namespace nfsmw::binding_table {

constexpr uintptr_t kLive = 0x91F418, kDefault = 0x8F6D80;
constexpr int kCount = 76;
constexpr unsigned kEntrySize = 0x34;

struct Slot {
    int type, device, object;
};
enum SlotType { kNone, kKey, kButton, kPov, kAxisLow, kAxisHigh, kRotLow, kRotHigh, kSliderLow, kSliderHigh };
inline Slot *slots_of(uintptr_t table, int entry) { return reinterpret_cast<Slot *>(table + entry * kEntrySize + 4); }
inline const char *&name_of(uintptr_t table, int entry) {
    return *reinterpret_cast<const char **>(table + entry * kEntrySize);
}

// Entries taken over by mods. 26, 28, 25, 27 (SC_A, SC_D, SC_W, SC_S): the camera look of nfsmw_controller_support.
constexpr int kPreviousCameraEntry = 67;  // SC_NUMPAD5: Previous Camera View of nfsmw_camera_settings

// Writes into the table's read-only-by-default memory.
inline void set_slots(uintptr_t table, int entry, const Slot *slots, int first, int count) {
    DWORD old = 0;
    Slot *dst = slots_of(table, entry);
    VirtualProtect(dst, 4 * sizeof(Slot), PAGE_READWRITE, &old);
    for (int i = first; i < first + count; ++i) dst[i] = slots[i];
    VirtualProtect(dst, 4 * sizeof(Slot), old, &old);
}
inline void set_name(uintptr_t table, int entry, const char *name) {
    DWORD old = 0;
    const char **at = &name_of(table, entry);
    VirtualProtect(at, sizeof(*at), PAGE_READWRITE, &old);
    *at = name;
    VirtualProtect(at, sizeof(*at), old, &old);
}

// "type device object" for each of the 4 slots, as the inis keep them.
inline bool parse_slots(const char *text, Slot out[4]) {
    int v[12];
    if (sscanf(text, "%d %d %d %d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
               &v[8], &v[9], &v[10], &v[11]) != 12)
        return false;
    for (int i = 0; i < 4; ++i) out[i] = Slot{v[i * 3], v[i * 3 + 1], v[i * 3 + 2]};
    return true;
}
inline void format_slots(const Slot *b, char *text, size_t n) {
    snprintf(text, n, "%d %d %d %d %d %d %d %d %d %d %d %d", b[0].type, b[0].device, b[0].object, b[1].type,
             b[1].device, b[1].object, b[2].type, b[2].device, b[2].object, b[3].type, b[3].device, b[3].object);
}

}  // namespace nfsmw::binding_table

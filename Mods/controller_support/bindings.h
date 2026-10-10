// The game's PC binding table, and the four camera look entries in it.
//
// The game's PC input device ("GameDevice"; only port 0 polls) has 76 binding entries: each is {name, 4 slots of
// {type, device, object}}; type 1 = keyboard key, 2 = button, 3 = POV, 4/5 = X/Y/Z axis (object 0-2) towards 0 /
// towards 65535, 6/7 = rotation axes, 8/9 = sliders. A slot's value comes from BindingValue (0x628940); axes give
// proportional values. The Controls menu edits slots; the game's action mapping reads the entries' values.
//
// Camera look: four entries no input mapping in the game data refers to (checked against GLOBAL/attributes.bin), so
// their values carry nothing else. They are bound to the right stick by default, in the default table (used by the
// Controls menu's Defaults) and in the live one. The profile saves only entries 0-18, so these four are kept in
// nfsmw_controller_support.ini [look_bindings].
#pragma once

#include "pad.h"

namespace pad::bindings {

constexpr uintptr_t kBindingTable = 0x91F418;
constexpr int kBindingCount = 76;
constexpr unsigned kEntrySize = 0x34;

struct Slot {
    int type, device, object;
};
enum SlotType { kNone, kKey, kButton, kPov, kAxisLow, kAxisHigh, kRotLow, kRotHigh, kSliderLow, kSliderHigh };
inline Slot *slots_of(uintptr_t table, int entry) { return reinterpret_cast<Slot *>(table + entry * kEntrySize + 4); }

enum Look { L_LEFT, L_RIGHT, L_UP, L_DOWN, L_COUNT };
constexpr int kLookEntry[L_COUNT] = {26, 28, 25, 27};  // SC_A, SC_D, SC_W, SC_S

extern float g_look_input[L_COUNT];  // 0..1 per direction, from the last poll
extern bool g_look_entries_ok;       // the four entries were found and renamed

void prepare_default_look_entries();                      // the default table, at start-up
void set_look_slots(uintptr_t table, int look, const Slot *slots, int first, int count);
void look_default_slots(int look, Slot out[4]);
void ensure_look_language();  // the Controls menu's names for the rows, in the language table
void resync_changes();        // takes the table as it is now as the one to compare with (after a swap)
void install_hooks();         // GameDevice::PollDevice

}  // namespace pad::bindings

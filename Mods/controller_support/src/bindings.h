// The game's PC binding table (see common/binding_table.h), and the four camera look entries in it.
//
// Camera look: four entries no input mapping in the game data refers to (checked against GLOBAL/attributes.bin), so
// their values carry nothing else. They are bound to the right stick by default, in the default table (used by the
// Controls menu's Defaults) and in the live one. The profile saves only entries 0-18, so these four are kept in
// nfsmw_controller_support.ini [look_bindings].
#pragma once

#include "binding_table.h"
#include "custom_menus_api.h"
#include "pad.h"

namespace pad::bindings {

using namespace nfsmw::binding_table;
constexpr uintptr_t kBindingTable = kLive;
constexpr int kBindingCount = kCount;

enum Look { L_LEFT, L_RIGHT, L_UP, L_DOWN, L_COUNT };
constexpr int kLookEntry[L_COUNT] = {26, 28, 25, 27};  // SC_A, SC_D, SC_W, SC_S

extern float g_look_input[L_COUNT];  // 0..1 per direction, from the last poll
extern bool g_look_entries_ok;       // the four entries were found and renamed

void prepare_default_look_entries();                      // the default table, at start-up
void set_look_slots(uintptr_t table, int look, const Slot *slots, int first, int count);
void look_default_slots(int look, Slot out[4]);
void provide_controls_rows(const nfsmw::custom_menus::Host *host);  // our rows, their names and the label hook, for nfsmw_custom_menus
void resync_changes();        // takes the table as it is now as the one to compare with (after a swap)
void install_hooks();         // GameDevice::PollDevice

}  // namespace pad::bindings

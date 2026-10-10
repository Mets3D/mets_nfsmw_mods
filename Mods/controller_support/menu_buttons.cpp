// The game's menu actions are binding entries too (FE_ACTION_ACCEPT, ...), and in the controller slot (slot 2) they
// are bound to the controller's buttons by number: Accept is button 1 (B), Cancel button 2 (X), and the "1" action
// button 0 (A). Xbox controllers do it the other way round: A accepts and B goes back, and Y takes the "1" action. The
// button numbers are changed in the default table and in the live one, which the game can rebuild from it.
//
// The same layout as the widescreen fix's ImproveGamepadSupport, which changes the same three numbers.
#include "menu_buttons.h"

#include "bindings.h"

namespace pad::menu_buttons {

namespace {

bool g_enabled = true;

struct Remap {
    int entry;
    const char *name;
    int button;  // 0 = A, 1 = B, 3 = Y
};
const Remap kRemaps[] = {
    {32, "FE_ACTION_ACCEPT", 0},
    {33, "FE_ACTION_CANCEL", 1},
    {41, "FE_ACTION_B5", 3},
};
constexpr int kControllerSlot = 2;
constexpr uintptr_t kDefaultBindingTable = 0x8F6D80;

// Changes the three buttons in a table, where the entry is the one we expect (name, and a button in the controller slot).
int patch_table(uintptr_t table) {
    int changed = 0;
    for (const Remap &r : kRemaps) {
        const char *name = *reinterpret_cast<const char **>(table + r.entry * bindings::kEntrySize);
        bindings::Slot &slot = bindings::slots_of(table, r.entry)[kControllerSlot];
        if (!name || strcmp(name, r.name) != 0 || slot.type != bindings::kButton || slot.object == r.button) continue;
        DWORD old = 0;
        VirtualProtect(&slot, sizeof(slot), PAGE_READWRITE, &old);
        slot.object = r.button;
        VirtualProtect(&slot, sizeof(slot), old, &old);
        ++changed;
    }
    return changed;
}

}  // namespace

void load_settings() {
    g_enabled = GetPrivateProfileIntA("buttons", "menu_layout", 1, g_ini) != 0;
    log_line("menu buttons: %s", g_enabled ? "A accepts, B goes back, Y is the \"1\" action" : "the game's own");
}

void patch_default_table() {
    if (g_enabled) log_line("menu buttons: default table, %d change(s)", patch_table(kDefaultBindingTable));
}

void apply_live() {
    static bool logged = false;
    if (!g_enabled) return;
    const int changed = patch_table(bindings::kBindingTable);
    if (changed && !logged) {
        logged = true;
        log_line("menu buttons: live table, %d change(s)", changed);
    }
}

}  // namespace pad::menu_buttons

#include "default_layout.h"

#include "bindings.h"

namespace pad::default_layout {

using namespace pad::bindings;

namespace {

bool g_enabled = true;

// Slot 2 of each entry (slot 3 is cleared), in the numbering the game gets from our XInput state: buttons 0 A, 1 B, 2 X, 3 Y,
// 4 LB, 5 RB, 6 View, 7 Menu, 8 left stick press, 9 right stick press; the D-pad is a hat (0 up, 1 right, 2 down, 3 left);
// the triggers are the two sliders we make of them (0 LT, 1 RT), pulling towards 0.
struct Binding {
    int entry;
    const char *names[2];  // what the entry may be called (a mod can have renamed it)
    Slot slot;
};
const Binding kLayout[] = {
    {0, {"GAME_ACTION_GAS"}, {kSliderLow, 0, 1}},      // Accelerate: RT
    {1, {"GAME_ACTION_BRAKE"}, {kSliderLow, 0, 0}},    // Brake / Reverse: LT
    {4, {"GAME_ACTION_HANDBRAKE"}, {kButton, 0, 4}},   // Handbrake: LB
    {5, {"GAME_ACTION_GAMEBREAKER"}, {kButton, 0, 2}}, // Speedbreaker: X
    {6, {"GAME_ACTION_NOS"}, {kButton, 0, 0}},         // N2O: A
    {7, {"GAME_ACTION_SHIFTDOWN"}, {kPov, 0, 2}},      // Shift Down: D-pad Down
    {8, {"GAME_ACTION_SHIFTUP"}, {kButton, 0, 5}},     // Shift Up: RB
    {9, {"GAME_ACTION_RESET"}, {kButton, 0, 9}},       // Reset Car Position: right stick press
    {10, {"HUDACTION_ENGAGE_EVENT"}, {kButton, 0, 8}}, // Engage Event: left stick press
    {11, {"HUDACTION_PAD_DOWN"}, {kButton, 0, 3}},     // View Blacklist: Y
    {12, {"HUDACTION_PAD_LEFT"}, {kButton, 0, 6}},     // View World Map: View
    {13, {"HUDACTION_PAD_RIGHT"}, {}},                 // View SMS Messages: unbound
    {15, {"CAM_ACTION_CHANGE"}, {kPov, 0, 1}},         // Next Camera View: D-pad Right
    {16, {"CAM_ACTION_LOOKBACK"}, {kButton, 0, 1}},    // Look Behind: B
    // Previous Camera View belongs to nfsmw_camera_settings, which leaves its controller binding to this layout (the D-pad is
    // free here; in the game's own layout D-pad Left is View World Map).
    {nfsmw::binding_table::kPreviousCameraEntry, {"SC_NUMPAD5", "CAM_ACTION_PREVIOUS"}, {kPov, 0, 3}},
};

bool expected(uintptr_t table, const Binding &b) {
    const char *name = name_of(table, b.entry);
    for (const char *n : b.names)
        if (n && name && strcmp(name, n) == 0) return true;
    return false;
}

int apply_to(uintptr_t table) {
    int changed = 0;
    for (const Binding &b : kLayout) {
        if (!expected(table, b)) {
            log_line("default layout: binding entry %d is \"%s\", not %s; left alone", b.entry,
                     name_of(table, b.entry) ? name_of(table, b.entry) : "?", b.names[0]);
            continue;
        }
        const Slot slots[4] = {{}, {}, b.slot, {}};
        set_slots(table, b.entry, slots, 2, 2);
        ++changed;
    }
    return changed;
}

// The game reset the controller bindings: the layout is the default again.
void on_controller_defaults() {
    if (g_enabled) log_line("default layout: %d binding(s) set after the game's defaults", apply_to(kBindingTable));
}

}  // namespace

void load_settings() {
    g_enabled = GetPrivateProfileIntA("buttons", "better_default_controller_bindings", 1, g_ini) != 0;
    log_line("default layout: %s", g_enabled ? "ours (RT / LT drive, ...)" : "the game's own");
}

void patch_default_table() {
    if (g_enabled) log_line("default layout: default table, %d change(s)", apply_to(kDefault));
}

void apply_live() {
    static bool done = false;
    if (done) return;
    done = true;
    if (g_enabled) log_line("default layout: live table, %d change(s)", apply_to(kBindingTable));
}

void provide(const nfsmw::custom_menus::Host *host) { host->add_controller_defaults(&on_controller_defaults); }

}  // namespace pad::default_layout

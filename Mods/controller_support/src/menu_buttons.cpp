// The game's menu actions are binding entries too (FE_ACTION_ACCEPT, ...), and in the controller slot (slot 2) they
// are bound to the controller's buttons by number: Accept is button 1 (B), Cancel button 2 (X), and the "1" action
// button 0 (A). Xbox controllers do it the other way round: A accepts and B goes back, and Y takes the "1" action. The
// button numbers are changed in the default table and in the live one, which the game can rebuild from it.
//
// The hotkeys "2", "3" and "4" of the menus (the "Advanced Options" of the video settings, the car Showcase, the rival's
// movie, "View Tutorial", the EA Trax preview...) are keys the game reads from a table of key states
// (0x514B90 looks a key up in it). A controller has no binding for them, so the buttons X, View and Menu set the key
// states themselves, the way the keys do. The main menu's "Quit" is the key Q: B presses it where a Quit prompt is on
// screen and no Back prompt (the main menu: B has nothing to go back to, and the Quit prompt shows B there).
//
// The same layout and hotkeys as the widescreen fix's ImproveGamepadSupport (which quits with both stick clicks).
#include "menu_buttons.h"

#include "bindings.h"
#include "button_icons.h"

namespace pad::menu_buttons {

namespace {


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

// The key-state table (a dword per key, 0x1C bytes apart) and the offsets of the keys "4", "3" and "2" in it.
constexpr uintptr_t kKeyStates = 0x8F3448;
constexpr unsigned kKey4 = 0xB0, kKey3 = 0xCC, kKey2 = 0x13C;
constexpr int kButtonB = 1, kButtonX = 2, kButtonView = 6, kButtonMenu = 7;
constexpr uintptr_t kGameState = 0x925E90;  // 3 = the main menu and the other menus
constexpr int kStateMenus = 3;

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

void patch_default_table() { log_line("menu buttons: default table, %d change(s)", patch_table(kDefaultBindingTable)); }

// The hotkeys 2 / 3 / 4 and Quit follow the buttons of every controller: a key is down while any of them holds its button.
void pad_buttons(int controller, const unsigned char buttons[32]) {
    if (controller < 0 || controller >= 8) controller = 0;
    struct Hotkey {
        int button;
        unsigned key;
        bool down[8];  // per controller
        bool set;      // what the key state was last set to
    };
    static Hotkey hotkeys[] = {{kButtonX, kKey2, {}, false}, {kButtonView, kKey3, {}, false}, {kButtonMenu, kKey4, {}, false}};
    for (Hotkey &h : hotkeys) {
        h.down[controller] = (buttons[h.button] & 0x80) != 0;
        bool any = false;
        for (const bool d : h.down) any |= d;
        if (any == h.set) continue;  // only when it changes, so that the keyboard can set the same key states
        h.set = any;
        *reinterpret_cast<int *>(kKeyStates + h.key) = any ? 1 : 0;
    }
    static bool b_down[8] = {};
    const bool b = (buttons[kButtonB] & 0x80) != 0;
    if (b && !b_down[controller] && *reinterpret_cast<int *>(kGameState) == kStateMenus && button_icons::quit_prompt_shown()) {
        keybd_event('Q', 0, 0, 0);
        keybd_event('Q', 0, KEYEVENTF_KEYUP, 0);
    }
    b_down[controller] = b;
}

void apply_live() {
    static bool logged = false;
    const int changed = patch_table(bindings::kBindingTable);
    if (changed && !logged) {
        logged = true;
        log_line("menu buttons: live table, %d change(s)", changed);
    }
}

}  // namespace pad::menu_buttons

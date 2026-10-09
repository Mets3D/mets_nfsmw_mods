// The Controls menu (UIOptionsPCController): extra rows for the camera look bindings, readable binding names, and its
// title icon.
#include "bindings.h"
#include "controls_menu.h"
#include "hooks.h"
#include "input.h"

namespace pad::controls_menu {

using namespace pad::bindings;

// ------------------------------------------------------------------------------------------------
// Controls menu rows (UIOptionsPCController). Rows are binding entries by index: 18 for the keyboard list (entries
// 0-17), 19 for the controller list (0-18). The four look rows follow them:
//  - the row count (0x12 + controller) is raised by 4 at the 8 places that compute it;
//  - the screen keeps a copy of each row's two bindings (0x18 bytes per row from +0xE8, to detect and undo changes),
//    room for 19 rows ending at its one-byte field at +0x2B0; the object grows from 0x2B4 to 0x314 bytes and that
//    field moves to +0x310;
//  - the helpers that turn a row into a binding entry translate the look rows to entries 25-28.
// ------------------------------------------------------------------------------------------------
struct CodePatch {
    uintptr_t addr;
    unsigned char expect[6];
    int len;
    int at;              // offset of the patched value
    unsigned value;
    int value_size;
};
const CodePatch kRowPatches[] = {
    {0x571D16, {0x68, 0xB4, 0x02, 0x00, 0x00}, 5, 1, 0x314, 4},  // new (0x2B4) in CreatePausePCController
    {0x52ACA1, {0x88, 0x97, 0xB0, 0x02, 0x00, 0x00}, 6, 2, 0x310, 4},  // the +0x2B0 byte: refresh
    {0x51D56D, {0x2A, 0x85, 0xB0, 0x02, 0x00, 0x00}, 6, 2, 0x310, 4},  // compare
    {0x51D645, {0x8A, 0x95, 0xB0, 0x02, 0x00, 0x00}, 6, 2, 0x310, 4},  // restore
    {0x52ACE8, {0x83, 0xC1, 0x12}, 3, 2, 0x12 + L_COUNT, 1},  // row counts: refresh
    {0x52AE1D, {0x83, 0xC0, 0x12}, 3, 2, 0x12 + L_COUNT, 1},
    {0x51D42E, {0x83, 0xC1, 0x12}, 3, 2, 0x12 + L_COUNT, 1},  // labels
    {0x51D498, {0x83, 0xC2, 0x12}, 3, 2, 0x12 + L_COUNT, 1},
    {0x51D4E7, {0x83, 0xC1, 0x12}, 3, 2, 0x12 + L_COUNT, 1},  // compare
    {0x51D555, {0x83, 0xC0, 0x12}, 3, 2, 0x12 + L_COUNT, 1},
    {0x51D5C7, {0x83, 0xC1, 0x12}, 3, 2, 0x12 + L_COUNT, 1},  // restore
    {0x51D636, {0x83, 0xC2, 0x12}, 3, 2, 0x12 + L_COUNT, 1},
};
bool g_rows_added = false;

void add_rows() {
    for (const CodePatch &c : kRowPatches) {
        if (memcmp(reinterpret_cast<const void *>(c.addr), c.expect, c.len) != 0) {
            log_line("controls rows: code at %#x is not as expected (another mod?); no look rows",
                     static_cast<unsigned>(c.addr));
            return;
        }
    }
    for (const CodePatch &c : kRowPatches) {
        unsigned char *at = reinterpret_cast<unsigned char *>(c.addr + c.at);
        DWORD old = 0;
        VirtualProtect(at, c.value_size, PAGE_EXECUTE_READWRITE, &old);
        if (c.value_size == 4) *reinterpret_cast<unsigned *>(at) = c.value;
        else *at = static_cast<unsigned char>(c.value);
        VirtualProtect(at, c.value_size, old, &old);
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    g_rows_added = true;
    log_line("controls rows: 4 look rows added to the Controls menu");
}

void *g_controls_screen = nullptr;  // the open UIOptionsPCController (+0xE4: 1 = keyboard list, 3 = controller)

int entry_for_row(int column, int row) {
    if (!g_rows_added || !g_look_entries_ok) return row;
    const int base = column == 1 ? 18 : 19;
    return row >= base && row < base + L_COUNT ? kLookEntry[row - base] : row;
}

int controls_column() { return g_controls_screen ? field<int>(g_controls_screen, 0xE4) : 3; }

bool from_controls_screen(void *return_address) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(return_address);
    return (a >= 0x51D410 && a < 0x51D740) || (a >= 0x52A7B0 && a < 0x52B300);
}

// Also fixes the screen's title icon: its layout (Pause_PC_Controller.fng) defaults the icon object to the audio
// icon, like the Options layout does, but unlike the Options screens this one never sets its own (a base game bug).
constexpr unsigned kHeaderIconObject = 0x08007B4C, kControlsIconTexture = 0xA04A7B26;  // OPTIONS_ICON_CONTROLS
const auto FEngSetTextureHashPkg = reinterpret_cast<void (*)(const char *, unsigned, unsigned)>(0x527EE0);

MethodFn g_orig_controls_refresh;
void FASTCALL hook_controls_refresh(void *screen, void *edx) {
    g_controls_screen = screen;
    if (g_rows_added && g_look_entries_ok) ensure_look_language();
    g_orig_controls_refresh(screen, edx);
    if (const char *pkg = field<const char *>(screen, 0x10)) FEngSetTextureHashPkg(pkg, kHeaderIconObject, kControlsIconTexture);
}

// Row -> the entry's binding slots. Also used by the profile save and load for entries 0-18, which must not move.
using RowSlotsFn = void *(__stdcall *)(int);
RowSlotsFn g_orig_row_slots;
void *__stdcall hook_row_slots(int row) {
    if (from_controls_screen(__builtin_return_address(0))) row = entry_for_row(controls_column(), row);
    return g_orig_row_slots(row);
}

// Row -> language hash of the entry's name (row title, and the "press a key" prompt).
using RowNameFn = unsigned (*)(int);
RowNameFn g_orig_row_name;
unsigned hook_row_name(int row) { return g_orig_row_name(entry_for_row(controls_column(), row)); }

// Clear a binding / start remapping one: GameDevice methods taking (column, row, primary).
using RowActionFn = void(FASTCALL *)(void *, void *, int, int, int);
RowActionFn g_orig_clear_binding, g_orig_start_remap;
void FASTCALL hook_clear_binding(void *device, void *edx, int column, int row, int primary) {
    g_orig_clear_binding(device, edx, column, entry_for_row(column, row), primary);
}
void FASTCALL hook_start_remap(void *device, void *edx, int column, int row, int primary) {
    g_orig_start_remap(device, edx, column, entry_for_row(column, row), primary);
}

// Controller defaults (0x63CD80, per controller model): also at start-up. The look rows get theirs back.
MethodFn g_orig_controller_defaults;
void FASTCALL hook_controller_defaults(void *device, void *edx) {
    g_orig_controller_defaults(device, edx);
    if (!g_look_entries_ok) return;
    for (int l = 0; l < L_COUNT; ++l) {
        Slot slots[4];
        look_default_slots(l, slots);
        set_look_slots(kBindingTable, l, slots, 2, 2);
    }
}

// Controls menu label of a controller binding, for an XInput controller.
bool controller_label(const Slot &b, char *out, size_t n) {
    static const char *const kButtons[] = {"A", "B", "X", "Y", "LB", "RB", "View", "Menu", "Left stick press",
                                           "Right stick press"};
    static const char *const kPovNames[] = {"D-pad Up", "D-pad Right", "D-pad Down", "D-pad Left"};
    const bool low = b.type == kAxisLow || b.type == kRotLow || b.type == kSliderLow;
    switch (b.type) {
        case kButton:
            if (b.object >= 0 && b.object < 10) snprintf(out, n, "%s", kButtons[b.object]);
            else snprintf(out, n, "Button %d", b.object + 1);
            return true;
        case kPov:
            if (b.object >= 0 && b.object < 4) snprintf(out, n, "%s", kPovNames[b.object]);
            else return false;
            return true;
        case kAxisLow:
        case kAxisHigh:
            if (b.object == 0) snprintf(out, n, "Left stick %s", low ? "Left" : "Right");
            else if (b.object == 1) snprintf(out, n, "Left stick %s", low ? "Up" : "Down");
            else snprintf(out, n, "%s (shared trigger axis)", low ? "RT" : "LT");
            return true;
        case kRotLow:
        case kRotHigh:
            if (b.object == 0) snprintf(out, n, "Right stick %s", low ? "Left" : "Right");
            else if (b.object == 1) snprintf(out, n, "Right stick %s", low ? "Up" : "Down");
            else return false;
            return true;
        case kSliderLow:
        case kSliderHigh:
            snprintf(out, n, "%s", b.object == 0 ? "LT" : "RT");
            return true;
        default:
            return false;
    }
}

// The Controls menu's label for one binding: (out, column: 1 keyboard / 3 controller, entry, primary). The game
// writes "Button 2" / "X Axis +" style text, which the widescreen fix turns into names without a direction.
using BindingLabelFn = void (*)(char *, int, int, bool);
BindingLabelFn g_orig_binding_label;


void hook_binding_label(char *out, int column, int entry, bool primary) {
    entry = entry_for_row(column, entry);  // only called by the Controls menu, with rows
    g_orig_binding_label(out, column, entry, primary);
    if (!g_cfg.labels || column == 1 || !out || entry < 0 || entry >= kBindingCount || !input::any_xinput_device()) return;
    char text[64];
    if (controller_label(slots_of(kBindingTable, entry)[primary ? 2 : 3], text, sizeof(text))) {
        strncpy(out, text, 0x103);
        out[0x103] = 0;
    }
}

bool rows_added() { return g_rows_added; }

void install_hooks() {
    const nfsmw::Hook hooks[] = {
        {"label", "binding label (Controls menu)", 0x628230, {0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x14}, false,
         nfsmw::detour(&hook_binding_label), nfsmw::orig(&g_orig_binding_label)},
        {"defaults", "controller defaults", 0x63CD80, {0xA1, 0x50, 0xF1, 0x91, 0x00, 0x81, 0xEC}, false,
         nfsmw::detour(&hook_controller_defaults), nfsmw::orig(&g_orig_controller_defaults)},
        {"refresh", "Controls menu refresh", 0x52AC70, {0x6A, 0xFF, 0x68, 0x78, 0xEF, 0x86, 0x00}, false,
         nfsmw::detour(&hook_controls_refresh), nfsmw::orig(&g_orig_controls_refresh)},
    };
    nfsmw::install_hooks(hooks, 3, g_log, nullptr, "controls menu");
    if (!g_rows_added) return;
    // The row helpers: a half-translated menu would edit the wrong bindings, so all four or none (the row patches
    // stay, which only shows four extra rows of entries 18-22).
    const nfsmw::Hook row_hooks[] = {
        {"row_slots", "row -> binding slots", 0x628CB0, {0x8B, 0x44, 0x24, 0x04, 0x6B, 0xC0, 0x34}, true,
         nfsmw::detour(&hook_row_slots), nfsmw::orig(&g_orig_row_slots)},
        {"row_name", "row -> name", 0x627BB0, {0x8B, 0x44, 0x24, 0x04, 0x6B, 0xC0, 0x34}, true,
         nfsmw::detour(&hook_row_name), nfsmw::orig(&g_orig_row_name)},
        {"clear", "clear binding", 0x628560, {0x8B, 0x44, 0x24, 0x04, 0x33, 0xD2, 0x83}, true,
         nfsmw::detour(&hook_clear_binding), nfsmw::orig(&g_orig_clear_binding)},
        {"remap", "start remap", 0x628C00, {0x8B, 0x44, 0x24, 0x04, 0x83, 0xF8, 0x01}, true,
         nfsmw::detour(&hook_start_remap), nfsmw::orig(&g_orig_start_remap)},
    };
    const nfsmw::HookResult r = nfsmw::install_hooks(row_hooks, 4, g_log, nullptr, "controls menu rows");
    if (!r.ok || r.created != 4) {
        log_line("controls rows: hooking the row helpers failed; the extra rows would edit the wrong entries");
        g_look_entries_ok = false;
    }
}

}  // namespace pad::controls_menu

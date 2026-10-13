// The Controls menu (UIOptionsPCController): direction names for XInput bindings, and the controller defaults of our entries.
// The rows themselves (the look rows) are added by nfsmw_custom_menus; see bindings.cpp for what we register.
#include "bindings.h"
#include "controls_menu.h"
#include "hooks.h"
#include "input.h"

namespace pad::controls_menu {

using namespace pad::bindings;

// Controller defaults (0x63CD80, per controller model): also at start-up. Our entries get theirs back.
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

// Called by nfsmw_custom_menus after the game wrote a binding's label: the widescreen fix turns the game's "Button 2" /
// "X Axis +" text into names without a direction, so an XInput controller's get proper ones here.
void binding_label(char *out, int column, int entry, bool primary) {
    if (!g_cfg.labels || column == 1 || !out || entry < 0 || entry >= kBindingCount || !input::any_xinput_device()) return;
    char text[64];
    if (controller_label(slots_of(kBindingTable, entry)[primary ? 2 : 3], text, sizeof(text))) {
        strncpy(out, text, 0x103);
        out[0x103] = 0;
    }
}

void install_hooks() {
    const nfsmw::Hook hooks[] = {
        {"defaults", "controller defaults", 0x63CD80, {0xA1, 0x50, 0xF1, 0x91, 0x00, 0x81, 0xEC}, false,
         nfsmw::detour(&hook_controller_defaults), nfsmw::orig(&g_orig_controller_defaults)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "controls menu");
}

}  // namespace pad::controls_menu

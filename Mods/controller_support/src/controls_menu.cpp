// The Controls menu (UIOptionsPCController): direction names for XInput bindings.
// The rows themselves (the look rows) are added by nfsmw_custom_menus; see bindings.cpp for what we register.
#include "bindings.h"
#include "button_icons.h"
#include "controls_menu.h"
#include "device_settings.h"
#include "input.h"
#include "pad_sets.h"

namespace pad::controls_menu {

using namespace pad::bindings;

using button_icons::Style;

// What the buttons of the game's XInput numbering are called on the controller's own face: the names follow the controller's button
// prompt style (button_icons.h), by position (as the pictures do): the Switch's B is the bottom button, the Xbox's A.
struct ButtonNames {
    const char *buttons[8];  // 0 bottom, 1 right, 2 left, 3 top, 4 left bumper, 5 right bumper, 6 View, 7 Menu
    const char *left_trigger, *right_trigger;
};
const ButtonNames &names_of(Style style) {
    static const ButtonNames kXbox = {{"A", "B", "X", "Y", "LB", "RB", "View", "Menu"}, "LT", "RT"};
    static const ButtonNames kPlayStation = {{"Cross", "Circle", "Square", "Triangle", "L1", "R1", "Share", "Options"}, "L2", "R2"};
    static const ButtonNames kSwitch = {{"B", "A", "Y", "X", "L", "R", "-", "+"}, "ZL", "ZR"};
    static const ButtonNames kSwitchNintendoLayout = {{"A", "B", "X", "Y", "L", "R", "-", "+"}, "ZL", "ZR"};  // the game's A is the Switch's A
    return style == Style::kPlayStation ? kPlayStation : style == Style::kSwitch ? kSwitch : style == Style::kSwitchNintendoLayout ? kSwitchNintendoLayout : kXbox;
}

// Controls menu label of a controller binding, for an XInput controller whose buttons are named as in `style`.
bool controller_label(const Slot &b, char *out, size_t n, Style style) {
    const ButtonNames &names = names_of(style);
    static const char *const kStickPresses[] = {"Left stick press", "Right stick press"};
    static const char *const kPovNames[] = {"D-pad Up", "D-pad Right", "D-pad Down", "D-pad Left"};
    const bool low = b.type == kAxisLow || b.type == kRotLow || b.type == kSliderLow;
    switch (b.type) {
        case kButton:
            if (b.object >= 0 && b.object < 8) snprintf(out, n, "%s", names.buttons[b.object]);
            else if (b.object == 8 || b.object == 9) snprintf(out, n, "%s", kStickPresses[b.object - 8]);
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
            else snprintf(out, n, "%s (shared trigger axis)", low ? names.right_trigger : names.left_trigger);
            return true;
        case kRotLow:
        case kRotHigh:
            if (b.object == 0) snprintf(out, n, "Right stick %s", low ? "Left" : "Right");
            else if (b.object == 1) snprintf(out, n, "Right stick %s", low ? "Up" : "Down");
            else return false;
            return true;
        case kSliderLow:
        case kSliderHigh:
            snprintf(out, n, "%s", b.object == 0 ? names.left_trigger : names.right_trigger);
            return true;
        default:
            return false;
    }
}

// Called by nfsmw_custom_menus after the game wrote a binding's label: the widescreen fix turns the game's "Button 2" /
// "X Axis +" text into names without a direction, so an XInput controller's get proper ones here.
void binding_label(char *out, int column, int entry, bool primary) {
    if (column == 1 || !out || entry < 0 || entry >= kBindingCount || !input::any_xinput_device()) return;
    char text[64];
    const int slot = input::xinput_slot_of_joystick(pad_sets::live_pad());  // the controller whose bindings the screen shows
    Style style = slot >= 0 ? device_settings::prompts(slot) : button_icons::default_style();
    if (style == Style::kOff) style = Style::kXbox;
    if (controller_label(slots_of(kBindingTable, entry)[primary ? 2 : 3], text, sizeof(text), style)) {
        strncpy(out, text, 0x103);
        out[0x103] = 0;
    }
}

}  // namespace pad::controls_menu

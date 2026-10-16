// What the game reads from the controller (DirectInput) and how it maps it to actions.
#pragma once

#include "pad.h"

namespace pad::input {

struct DeviceInstance {  // DIDEVICEINSTANCEA
    DWORD size;
    GUID instance, product;
    DWORD type;  // the low byte is the DirectInput 8 device type: 0x12 mouse, 0x13 keyboard, 0x14-0x18 game controllers
    char instance_name[260], product_name[260];
    GUID ff_driver;
    WORD usage_page, usage;
};

// Hooks DirectInput's GetDeviceState. Done on the game's first input poll rather than at load time, so DirectInput is
// never started from inside the loader.
void hook_dinput();
bool any_xinput_device();        // an XInput controller has been seen
bool only_xinput_controllers();  // every DirectInput joystick seen is an XInput controller
int vehicle_source();            // the device that drives player 1's vehicle: an XInput slot, kSourceKeyboard or kDefault (all)
int player_one_rumble_slot();    // the XInput controller player 1 rumbles on, or -1
bool identity_of_slot(int slot, char *out, size_t size);  // what tells the controller in an XInput slot from others of its kind, if anything
int xinput_slot_of_joystick(int index);  // the XInput controller a game controller reports, or -1
int rumble_slot_of_joystick(int index);  // the XInput controller it rumbles through (the one in use when it is the only one), or -1
bool key_down(int dik);  // a key of the real keyboard (whether or not it reaches the game)
float binding_value(int type, int object, const xinput::Gamepad &pad, int slot);  // a controller binding, read from XInput controller `slot`
// The last thing the player touched was a controller (not the keyboard or the mouse).
bool last_input_was_pad();
// The XInput controller (0-3) that was touched last, or -1 for a controller that is not XInput (or when the slot cannot be told apart
// and none is in use). Meaningful while last_input_was_pad().
int last_pad_slot();
// The connected XInput controllers as one (sticks and triggers from the one pushing furthest); false when there is none.
bool read_merged(xinput::Gamepad *out);
// The mouse movement since the last call (the camera look reads it).
void take_mouse_delta(LONG *dx, LONG *dy);
void install_hooks();  // InputMapping

}  // namespace pad::input

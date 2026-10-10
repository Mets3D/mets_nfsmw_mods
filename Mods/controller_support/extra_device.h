// A second game input device, so that a second local player has an input path of the game's own.
//
// The game's input module (IOModule, 0x920DE0) holds its devices in an array, and every player's ActionQueue reads the
// device at its port; on PC that array has the GameDevice (port 0: the keyboard and all controllers), the mouse and the
// keyboard. A second GameDevice added to the array gives a second player a port of their own: their queue turns what the
// device says into the game's actions (steering, gas, camera change, pause, ...) and nothing has to be fed to the car by
// hand. The device says what the bindings (the Controls menu's) of its source ask for: an XInput controller or the keyboard.
#pragma once

#include "pad.h"

namespace pad::extra_device {

// Makes the device (once) and gives it a source: an XInput controller (0-3), controller_api::kSourceKeyboard or
// kSourceNone. Returns the port the second player's ActionQueue must read, or -1 when there is no device.
int add(int source);
int port();                 // the device's port, or -1 before add()
// The port a player must read: the second player (joystick port -1: the FE input of the PC game has no queue for other
// ports and crashes on them, so the player's database port stays -1) gets the device's port while it has a source.
int port_for_player(int joystick_port);
int source_slot();               // the XInput controller of the device's source (rumble, camera look), or -1
bool keyboard_source();         // the source is the keyboard (the mouse then looks around in this player's camera)
float value(int entry);         // what the device says for a binding entry now (0..1)
bool is_extra(void *device);  // a GameDevice that is this one
void poll(void *device);      // PollDevice of this device: its values from the source's bindings

}  // namespace pad::extra_device

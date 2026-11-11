// The players' game input devices: what each player drives with.
//
// The PC game has one input device for driving (the GameDevice at port 0 of its input module: the keyboard and every controller together), and each player's
// ActionQueue reads the device at the player's port. Here every player after the first gets a GameDevice of their own, at a port of its own, that says what
// that player's device alone asks for, so the game's own input path serves them (steering, camera change, pause ...); and player 1's vehicle entries of the
// port 0 device are taken from player 1's device alone. Menus are not touched: every device reaches them.
//
// What a device asks for: the keyboard through the game's key bindings, Keyboard 2 (keyboard2.h) through its own, a controller through its bindings in
// mets_controller_support (which is told who has which device, for its rumble and camera look), or, without that mod, through a fixed layout.
// No game function is hooked for this (mets_controller_support hooks the device's poll): the devices get a virtual table of their own.
#pragma once

#include "max_players.h"

namespace split_screen_support::player_devices {

constexpr int kSlots = nfsmw::kMaxPlayers - 1;  // slot 0 is player 2's, slot k player k + 2's

// A device is one of players.h: an XInput controller 0-3, kKeyboard or kKeyboard2; anything else is no device.
void first_player(int device);   // player 1's vehicle (and its camera, HUD actions and pause request) from this device alone; no device: from every device
int give(int slot, int device);  // the player of a slot reads this device (their game input device is made the first time). Returns its port, -1 when there is none
int port(int slot);              // the port of the slot's game input device while the player has a device, else -1

// The keyboard as DirectInput shows it (256 bytes, bit 7 = down); false when it cannot be read (the game's window is not in front).
bool read_keyboard(unsigned char *out);

// For automated tests (dev_tools): the game input device of a slot says this whatever the player's device does.
void test_input(int slot, bool on, int gas, int steer);  // gas and steering in thousandths (steering -1000 left to 1000 right), until `on` is false
void test_entry(int slot, int entry, int milli);         // a binding entry (agent_docs/input-system.md) in thousandths, until -1
void test_pause(int slot, bool down);                    // the pause request

}  // namespace split_screen_support::player_devices

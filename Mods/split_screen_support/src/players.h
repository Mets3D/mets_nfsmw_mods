// Who plays with which device. The PC game reads every controller and the keyboard as one device for player 1, so it
// cannot tell the players apart; this part can: XInput tells the controllers apart, and the keyboard is read by player_devices.h.
#pragma once

#include "xinput.h"

namespace split_screen_support::players {

constexpr int kKeyboard = -1;  // a device: the keyboard; 0-3 are XInput controllers
constexpr int kKeyboard2 = -5;  // a second set of keys on the same keyboard

enum class KeyboardMenuAction { kStart, kInteraction2 };
bool keyboard_menu_down(const unsigned char *keys, int device, KeyboardMenuAction action, bool separate_keypad);
const char *describe(int device);

// The game is a split screen game: Quick Race with two players (FEDatabase).
bool split_screen();

// The devices that pressed Start for players 1 and 2 (-2 for a player who has not), and putting them back (more_players.cpp keeps the
// first round's while the second round's are chosen).
void get_chosen(int out[2]);
void set_chosen(const int in[2]);

// The game input device port of player 3 or 4 (extra 0 or 1), -1 when they have none (see players.cpp).
int extra_port(int extra);
int second_port();  // the same for player 2

// The device that pressed Start on the Press Start screen of player `index` (0 or 1) of this round: -2 when none did.
int chosen(int index);
// The device's name as the Controls menu shows it ("Xbox One S Controller", "Keyboard"); without mets_controller_support only Keyboard 2 has one.
void device_label(int device, char *out, unsigned size);

// The device of player 0 to 3 in this race (0-3 an XInput controller, or kKeyboard).
int device(int player);
// The keyboard as DirectInput shows it (256 bytes, bit 7 = down); false when it cannot be read.
bool read_keyboard(unsigned char *out);

// Tells the Press Start screen that a device let go of Start (what the real device does; for automated tests). Needs the screen showing.
void tell_press_start(int device);

void install();  // registers with events, hooks the Press Start screen

}  // namespace split_screen_support::players

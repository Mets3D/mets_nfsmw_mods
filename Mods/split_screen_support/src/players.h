// Who plays with which device. The PC game reads every controller and the keyboard as one device for player 1, so it
// cannot tell the players apart; this part can: XInput tells the controllers apart, and the keyboard is seen
// through mets_controller_support.
#pragma once

#include "xinput.h"

namespace split_screen_support::players {

constexpr int kKeyboard = -1;  // a device: the keyboard; 0-3 are XInput controllers

// The game is a split screen game: Quick Race with two players (FEDatabase).
bool split_screen();

// The devices that pressed Start for players 1 and 2 (-2 for a player who has not), and putting them back (more_players.cpp keeps the
// first round's while the second round's are chosen).
void get_chosen(int out[2]);
void set_chosen(const int in[2]);

// The device of player 0 or player 1 in this race (0-3 an XInput controller, or kKeyboard).
int device(int player);
// The keyboard as DirectInput shows it (256 bytes, bit 7 = down), whoever it reaches; false when it is not known.
bool read_keyboard(unsigned char *out);

// Tells the Press Start screen that a device let go of Start (what the real device does; for automated tests). Needs the screen showing.
void tell_press_start(int device);

void install();  // registers with events, hooks the Press Start screen

}  // namespace split_screen_support::players

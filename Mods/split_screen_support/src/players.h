// Who plays with which device. The PC game reads every controller and the keyboard as one device for player 1, so it
// cannot tell the players apart; this part can: XInput tells the controllers apart, and the keyboard is seen
// through mets_controller_support.
#pragma once

#include "xinput.h"

namespace split_screen_support::players {

constexpr int kKeyboard = -1;  // a device: the keyboard; 0-3 are XInput controllers

// The game is a split screen game: Quick Race with two players (FEDatabase).
bool split_screen();

// The device of player 0 or player 1 in this race (0-3 an XInput controller, or kKeyboard).
int device(int player);
// The keyboard as DirectInput shows it (256 bytes, bit 7 = down), whoever it reaches; false when it is not known.
bool read_keyboard(unsigned char *out);

void install();  // registers with events, hooks the Press Start screen

}  // namespace split_screen_support::players

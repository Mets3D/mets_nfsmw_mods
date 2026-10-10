// Who plays with which controller. The PC game reads every controller and the keyboard as one device for player 1, so
// it cannot tell the players apart; this part can, through XInput.
#pragma once

#include "xinput.h"

namespace split_screen_support::players {

constexpr int kKeyboard = -1;

// The game is a split screen game: Quick Race with two players (FEDatabase).
bool split_screen();

// The XInput slot (0-3) of player 0's or player 1's controller in this race, or kKeyboard.
int pad(int player);
// That controller's state; false for the keyboard or a controller that is not connected.
bool read_pad(int player, nfsmw::xinput::Gamepad *out);

void install();  // registers with events, hooks the Press Start screen

}  // namespace split_screen_support::players

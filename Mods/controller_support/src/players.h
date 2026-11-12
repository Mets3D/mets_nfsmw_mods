// Who plays with which device, as another mod says it (nfsmw_split_screen_support, which hands the devices out and makes the players' game input
// devices: mets_controller_support_player_device, common/controller_api.h). Here it decides whose controller a player's rumble goes to and whose
// camera a device looks around in. Until a mod says otherwise there is one player, who has every device.
#pragma once

#include "pad.h"

namespace pad::players {

int source(int player);      // an XInput controller 0-3, kSourceKeyboard or kSourceNone; player 1 (0) can also have kDefault: every device
int controller(int player);  // their XInput controller, or -1
int of_port(int port);       // the player after the first whose game input device is at this port of the game's input module, or -1
int on_keyboard();           // the player who plays on the keyboard (the mouse is theirs), or -1
float value(int player, int entry);  // what a player's game input device says for a binding entry now (0..1)

}  // namespace pad::players

// The Car Select screen of a split screen race says whose it is and with which device (see car_select_label.cpp).
#pragma once

namespace split_screen_support::car_select_label {

// `package`: the screen's ("Car_Select.fng"). `player`: "Player 3". `device`: the device's name as the Controls menu shows it; empty: no row for it.
void show(const char *package, const char *player, const char *device);

}  // namespace split_screen_support::car_select_label

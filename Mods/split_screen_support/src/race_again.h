// The next split screen race with the same cars: Accept on the race options starts it, and "2 Select Car" lets one player choose another car (see race_again.cpp).
#pragma once

namespace split_screen_support::race_again {

void install();  // [options] race_again=0 turns it off

// For automated tests: the race options showing get "this device pressed Select Car" (0-3 an XInput controller, -1 the keyboard).
void test_select_car(int device);

}  // namespace split_screen_support::race_again

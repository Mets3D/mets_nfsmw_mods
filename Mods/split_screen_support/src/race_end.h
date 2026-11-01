// The end of a split screen race: every player gets the race-over message and the results wait a few seconds (see race_end.cpp).
#pragma once

namespace split_screen_support::race_end {

void install();

// A race is being made: nobody has had their message yet.
void race_started();

}  // namespace split_screen_support::race_end

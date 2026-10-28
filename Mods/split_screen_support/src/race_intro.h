// The race's intro cutscene in a split screen race (see race_intro.cpp).
#pragma once

namespace split_screen_support::race_intro {

// The intro cutscene of the race is playing: it is for the whole screen (split_view.cpp draws one picture).
bool playing();

void install();

}  // namespace split_screen_support::race_intro

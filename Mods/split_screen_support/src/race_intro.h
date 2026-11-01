// The race's intro cutscene in a split screen race (see race_intro.cpp).
#pragma once

namespace split_screen_support::race_intro {

// The intro cutscene of the race is playing: it is for the whole screen (split_view.cpp draws one picture).
bool playing();

// The intro cutscene is allowed in a split screen race ([options] intro_cutscene).
bool enabled();

// The race (split screen or not) has asked for its intro cutscene and it is not over: it will move the first four racers, wherever they stand until then.
bool expected();

void install();

}  // namespace split_screen_support::race_intro

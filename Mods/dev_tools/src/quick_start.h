// Jumping straight to the main menu at startup, past the EA logo and the intro movies.
#pragma once

namespace dev_tools::quick_start {

// Skipping the intro movies: in DllMain, before the game gets going.
void install_early();
// The rest (auto load).
void install();

}  // namespace dev_tools::quick_start

// The engine sounds of the cars (see engine_sounds.cpp).
#pragma once

namespace split_screen_support::engine_sounds {

// Writes what the sound system holds about the cars to the log (the car states, the managers' states, the engine banks): for bug reports about missing engine sounds.
void log_state(const char *why);

void install();

}  // namespace split_screen_support::engine_sounds

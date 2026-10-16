// Game functions more than one part of the mod needs to hear about. Each is hooked once, here, and calls the parts
// that asked, in the order they asked.
#pragma once

namespace split_screen_support::events {

using Callback = void (*)();

void on_world_load(Callback callback);  // BeginWorldLoad: a race is about to load (called before the game's own)
void on_frame(Callback callback);       // eDisplayFrame: once per frame, before the game draws it

// After the parts have asked.
void install();

}  // namespace split_screen_support::events

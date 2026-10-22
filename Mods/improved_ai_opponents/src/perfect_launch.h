// The improved AI's throttle at the starting line, for a perfect launch (see perfect_launch.cpp).
#pragma once

namespace improved_ai_opponents::perfect_launch {

// The throttle (0 to 1) for player `player`'s car while it is staging: its engine is at `rpm`, the revs of a perfect launch are `start` to `start + range` (what
// IRaceEngine::GetPerfectLaunchRange gives), `dt` seconds since the last call. `fallback` is what the game would have used (when there is no window to aim at).
float throttle(int player, float rpm, float start, float range, float dt, float fallback);

}  // namespace improved_ai_opponents::perfect_launch

// The views (eViews, 0x9195E0 + 0x70 * id) of the cameras of players 1 to 4. The game has views 1 and 2 for players 1 and 2; nfsmw_split_screen_support puts
// the game's own camera director and camera mover of players 3 and 4 into two views nothing else uses (no camera, no render target, never active), so a
// mod that looks at a camera mover (its view id is at +0x10) can tell whose it is.
#pragma once

namespace nfsmw::views {

constexpr int kPlayerView[4] = {1, 2, 12, 13};

// The player (0 to 3) whose camera is in `view`, or -1.
inline int player_of_view(int view) {
    for (int i = 0; i < 4; ++i)
        if (kPlayerView[i] == view) return i;
    return -1;
}

}  // namespace nfsmw::views

// The views (eViews, 0x9195E0 + 0x70 * id) of the cameras of the players. The game has views 1 and 2 for players 1 and 2; nfsmw_split_screen_support puts
// the game's own camera director and camera mover of the other players into views nothing else uses (no camera, no render target, never active), so a
// mod that looks at a camera mover (its view id is at +0x10) can tell whose it is. (PC view ids: 3 the rear view mirror, 4 the glow view, 8 the front end's car
// list, 15 and 16 the shadow maps, 18 to 23 the cube map; 5 to 7, 9 to 14, 17 have no camera.)
#pragma once

#include "max_players.h"

namespace nfsmw::views {

constexpr int kPlayerView[kMaxPlayers] = {1, 2, 12, 13, 14, 9, 10, 11};

// The player (0-based) whose camera is in `view`, or -1.
inline int player_of_view(int view) {
    for (int i = 0; i < kMaxPlayers; ++i)
        if (kPlayerView[i] == view) return i;
    return -1;
}

}  // namespace nfsmw::views

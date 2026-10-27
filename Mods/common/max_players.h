// How many players the split screen mods are sized for: per-player arrays, the views and the devices of the extra players. A mod that keeps data per player
// sizes it with kMaxPlayers and loops over the players there are, never over a literal 2 or 4, so more players are a change of this number (and a test), not a
// rewrite. What the game itself limits is noted where it is dealt with: its Press Start and Car Select screens know two players (kPlayersPerRound), it keeps
// settings for two players (the others' are kept by nfsmw_split_screen_support in its ini), and its track streamer has two places (so cars far apart pop in).
#pragma once

namespace nfsmw {

constexpr int kMaxPlayers = 8;
constexpr int kPlayersPerRound = 2;                         // the game's own screens, and its CreatePlayers / CreateCars calls: two players at a time
constexpr int kMaxExtras = kMaxPlayers - kPlayersPerRound;  // the players the game does not have: player 3 and up

}  // namespace nfsmw

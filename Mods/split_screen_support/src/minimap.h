// The minimaps of a split screen race: one for each player (hud_players.cpp makes them), each showing every other player, a colour for each player's marker.
#pragma once

namespace split_screen_support::minimap {

// Moves the map picture, ring and backdrop of a minimap by `dy` FE units (negative: up) in the HUD package `package`, and keeps them there (the layout's
// scripts put them back). The markers are placed by the minimap around its map centre, which moves with them.
void place(void *minimap, const char *package, float dy);

// The markers of a minimap (hud_players.cpp makes them): its own marker has the colour `own_rgb` and
// the other markers follow `others` (IPlayers) in `other_rgb`: the first is the minimap's second marker, the rest copies of it in the HUD's `package`. Forgotten when the HUDs go.
void set_markers(void *minimap, const char *package, unsigned own_rgb, const void *const *others, const unsigned *other_rgb, int count);

// Calls `fn` with every FEng object of a minimap (the ones its code moves about: the map picture, the ring, the markers...).
void for_each_object(void *minimap, void (*fn)(void *object));

// Logs the objects of an FEng package (a tree: name hash, type, position, size, flags): for working out a HUD's layout. ([options] dump_hud=1)
void dump_package(const char *package);

void install();

}  // namespace split_screen_support::minimap

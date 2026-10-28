// The HUD of each player: each player's HUD package is drawn into that player's cell of the screen (the whole screen for one player), each corner of the HUD
// anchored to the corner of the cell and scaled by the player's HUD scale.
#pragma once

namespace split_screen_support::hud_views {

// The HUD package `name` ("HUD_Player1.fng") belongs to `player` (0-based) of `players`; while it is drawn the screen is that player's cell. A package whose name
// starts with HUD_Player is made for the top half of the screen (the split screen HUD), any other for the whole screen.
void assign(const char *package, int player, int players);

// Works out which corner of the screen each object of a HUD package belongs to (from where the HUD's layout puts it), so each can be anchored to its own
// corner of the cell. Call once the HUD is made (and its minimap placed).
void classify(const char *package);

// The object (and what is in it) is part of a minimap: its parts are moved about by the minimap's code (the markers of the other racers and the like are put
// where they are on the map, wherever the layout had them), so all of it belongs to the minimap's corner, the bottom left.
void classify_as_minimap(void *object);

// An object that was made later (a copy of a minimap marker) belongs where `model` does.
void classify_like(void *object, void *model);
// The part of the HUD (and so the viewport) an object was given: -1 when it has none, 255 hidden, 254 the divider. For the log.
int part_of_object(const void *object);

// A player's HUD scale in percent (30 to 100), relative to the base scale of the player's cell (layout.h: the HUD as the game has it in a cell of half the screen or
// more, half that in a quarter); a corner keeps its place in the cell.
void set_scale(int player, int percent);
int scale(int player);

// Forget all assignments (the race is over).
void clear();

void install();

}  // namespace split_screen_support::hud_views

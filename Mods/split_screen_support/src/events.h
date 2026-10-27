// Game functions more than one part of the mod needs to hear about. Each is hooked once, here, and calls the parts
// that asked, in the order they asked.
#pragma once

namespace split_screen_support::events {

using Callback = void (*)();

void on_world_load(Callback callback);  // BeginWorldLoad: a race is about to load (called before the game's own)
void on_frame(Callback callback);       // eDisplayFrame: once per frame, before the game draws it

// The HUD of a player (FEngHud, made for each local player when a race starts; `type` is ePlayerHudType, 3 and 4 for the two players of a split screen
// race; `package` is the FEng package it shows).
using HudFilter = void (*)(int &type, const char *&package);       // before the HUD is made: may change what is made (the package string must stay valid)
using HudMade = void (*)(void *hud, int type, const char *package, void *player, int number);  // number: the player's settings index
using HudDestroy = void (*)(void *hud);                            // before it goes
using HudUpdate = void (*)(void *hud, void *player);               // around the game's own update of it (either callback may be null)
void on_hud_construct(HudFilter callback);
void on_hud_made(HudMade callback);
void on_hud_destroy(HudDestroy callback);
void on_hud_update(HudUpdate before, HudUpdate after);

// After the parts have asked.
void install();

}  // namespace split_screen_support::events

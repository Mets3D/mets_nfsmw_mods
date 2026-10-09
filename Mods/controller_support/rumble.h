// Rumble: the game's force feedback events (road surface, tire slip and skid, engine revs, NOS, gear changes, blown
// engine, collisions) drive an XInput controller's motors. See rumble.cpp for how.
#pragma once

#include "pad.h"

namespace pad::rumble {

// The effects, in menu order. Each has a level, 0-100: how hard the motors run in an ordinary instance of the effect
// (a solid hit, a normal drift, the moment NOS kicks in; bigger ones stay at that level); 0 is off.
enum Effect {
    E_COLLISIONS, E_LANDINGS, E_SHIFTS, E_NOS, E_DRIFT, E_WHEELSPIN, E_REV_LIMITER, E_ROAD, E_ENGINE, E_TIRE_BLOWOUT,
    E_ENGINE_BLOWN, E_COUNT
};
struct EffectInfo {
    const char *key, *label, *help;
    int def;  // 0-100
};
extern const EffectInfo kEffects[E_COUNT];
extern float g_level[E_COUNT];  // 0..1

bool enabled();         // [rumble] enabled, and XInput can vibrate
void load_settings();   // [rumble] from the ini
void save_levels();     // to the ini
void play_preview(int effect);  // a sample of the effect at its level (the menu uses it)
void start();           // the thread that talks to the controller
void install_hooks();

}  // namespace pad::rumble

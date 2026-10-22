// The settings of each player's AI: what the Options > AI Driving page edits (menu.cpp) and the ini keeps ([player_1] / [player_2]).
#pragma once

namespace improved_ai_opponents::settings {

enum Control { kOff, kVanilla, kImproved };  // who drives: a human, the game's AI as it is, the AI with our changes
enum Shortcuts { kAvoid, kAllowed, kAlways };
enum Overlay { kOverlayOff, kOverlayAi, kOverlayRoads, kOverlayCost };  // what is drawn in the player's picture: nothing, what the AI calculates, the road network, a route's cost  // the improved AI: the game's rule, plans them when they are the better route, prefers them

struct Player {
    int control = kOff;
    int skill = 100;  // 0..100 (the game's 0..1)
    bool fair_nitrous = true;      // the improved AI: uses nitrous as far as the human players have it (the game's rule); off: whatever its car has
    bool smarter_nitro = true;     // the improved AI: burns nitrous where the planned route lets the car use it (nitro.cpp), not by the game's rule of the car's state
    bool perfect_launch = true;    // the improved AI: holds the revs in the perfect launch window at the starting line (perfect_launch.cpp)
    bool fair_performance = true;  // the improved AI: does not go faster or hold more grip than the weakest player's car (the game's rule); off: as its own car can
    int shortcuts = kAllowed;
    bool follow_race_route = true;  // the improved AI: the nav of its car stays on the race's own route like the opponents' (the game's race filter), or may be on any road
    bool super_steer = true;  // the improved AI: arbitrary steering like the vanilla AI's; off = its steering is limited like a human player's (speed, rate)
    bool avoid_props = false;  // the improved AI: steers around the heavy props (barrels, posts) near its way (obstacles.cpp), and the AI Logic overlay draws them
    bool catch_up = false;
    int overlay = kOverlayOff;  // F8 steps through them; the AI's calculations are drawn also while a human drives (the improved AI then plans beside the car)
};

Player &player(int who);  // 0 = player 1, 1 = player 2
Player defaults(int who);

void load();
void save(int who);  // writes the player's settings to the ini

const char *control_name(int control);
const char *shortcuts_name(int shortcuts);
const char *overlay_name(int overlay);

}  // namespace improved_ai_opponents::settings

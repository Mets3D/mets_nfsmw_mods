// The settings of each player's AI: what the Options > AI Driving page edits (menu.cpp) and the ini keeps ([player_1] / [player_2]).
#pragma once

namespace improved_ai_opponents::settings {

enum Control { kOff, kVanilla, kImproved };  // who drives: a human, the game's AI as it is, the AI with our changes
enum Nitro { kNitroDisabled, kNitroFair, kNitroAllowed };  // the improved AI: never, as much as the other players' nitrous allows (the game's rule), always
enum Shortcuts { kAvoid, kAllowed, kAlways };  // the improved AI: the game's rule, plans them when they are the better route, prefers them

struct Player {
    int control = kOff;
    int skill = 100;  // 0..100 (the game's 0..1)
    int nitro = kNitroFair;
    int shortcuts = kAllowed;
    bool follow_race_route = true;  // the improved AI: the nav of its car stays on the race's own route like the opponents' (the game's race filter), or may be on any road
    bool super_steer = true;  // the improved AI: arbitrary steering like the vanilla AI's; off = its steering is limited like a human player's (speed, rate)
    bool catch_up = false;
    bool overlay = true;  // draws the AI's route for this player's car, also while a human drives it (the improved AI plans beside the car)
};

Player &player(int who);  // 0 = player 1, 1 = player 2
Player defaults(int who);

void load();
void save(int who);  // writes the player's settings to the ini

const char *control_name(int control);
const char *nitro_name(int nitro);
const char *shortcuts_name(int shortcuts);

}  // namespace improved_ai_opponents::settings

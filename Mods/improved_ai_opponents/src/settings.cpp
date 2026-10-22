#include "settings.h"

#include <cstdio>
#include <cstring>

#include "improved_ai_opponents.h"

namespace improved_ai_opponents::settings {

namespace {

Player g_players[2];

const char *const kControls[] = {"off", "vanilla", "improved"};
const char *const kShortcuts[] = {"avoid", "allowed", "always"};
const char *const kOverlays[] = {"off", "ai_logic", "roads", "cost"};

const char *section_of(int who) { return who == 0 ? "player_1" : "player_2"; }

template <size_t N> int read_choice(const char *section, const char *key, const char *const (&names)[N], int fallback) {
    char text[32] = {};
    GetPrivateProfileStringA(section, key, names[fallback], text, sizeof(text), g_ini);
    for (size_t i = 0; i < N; ++i)
        if (!_stricmp(text, names[i])) return static_cast<int>(i);
    return fallback;
}

}  // namespace

Player &player(int who) { return g_players[who != 0 ? 1 : 0]; }

Player defaults(int who) {
    Player p;
    p.control = who == 0 ? kOff : kImproved;  // player 2 is the AI's, player 1 drives (and watches the AI plan beside the car)
    return p;
}

void load() {
    for (int who = 0; who < 2; ++who) {
        const char *section = section_of(who);
        const Player d = defaults(who);
        Player &p = g_players[who];
        p.control = read_choice(section, "control", kControls, d.control);
        p.skill = GetPrivateProfileIntA(section, "skill", d.skill, g_ini);
        if (p.skill < 0 || p.skill > 100) p.skill = d.skill;
        p.fair_nitrous = ini_flag(section, "fair_nitrous", d.fair_nitrous);
        p.smarter_nitro = ini_flag(section, "smarter_nitro", d.smarter_nitro);
        p.fair_performance = ini_flag(section, "fair_performance", d.fair_performance);
        p.shortcuts = read_choice(section, "shortcuts", kShortcuts, d.shortcuts);
        p.follow_race_route = ini_flag(section, "follow_race_route", d.follow_race_route);
        p.super_steer = ini_flag(section, "super_steer", d.super_steer);
        p.avoid_props = ini_flag(section, "avoid_props", d.avoid_props);
        p.catch_up = ini_flag(section, "catch_up", d.catch_up);
        p.overlay = read_choice(section, "overlay", kOverlays, d.overlay);
    }
}

void save(int who) {
    const char *section = section_of(who);
    const Player &p = g_players[who];
    char text[16];
    snprintf(text, sizeof(text), "%d", p.skill);
    WritePrivateProfileStringA(section, "control", kControls[p.control], g_ini);
    WritePrivateProfileStringA(section, "skill", text, g_ini);
    WritePrivateProfileStringA(section, "fair_nitrous", p.fair_nitrous ? "1" : "0", g_ini);
    WritePrivateProfileStringA(section, "smarter_nitro", p.smarter_nitro ? "1" : "0", g_ini);
    WritePrivateProfileStringA(section, "fair_performance", p.fair_performance ? "1" : "0", g_ini);
    WritePrivateProfileStringA(section, "shortcuts", kShortcuts[p.shortcuts], g_ini);
    WritePrivateProfileStringA(section, "follow_race_route", p.follow_race_route ? "1" : "0", g_ini);
    WritePrivateProfileStringA(section, "super_steer", p.super_steer ? "1" : "0", g_ini);
    WritePrivateProfileStringA(section, "avoid_props", p.avoid_props ? "1" : "0", g_ini);
    WritePrivateProfileStringA(section, "catch_up", p.catch_up ? "1" : "0", g_ini);
    WritePrivateProfileStringA(section, "overlay", kOverlays[p.overlay], g_ini);
    g_log.line("player %d's AI settings saved: control %s, skill %d, fair nitrous %s, smarter nitro %s, fair performance %s, shortcuts %s, race route %s, super steer %s, avoid props %s, catch-up %s, overlay %s", who + 1, kControls[p.control], p.skill,
               p.fair_nitrous ? "on" : "off", p.smarter_nitro ? "on" : "off", p.fair_performance ? "on" : "off", kShortcuts[p.shortcuts], p.follow_race_route ? "followed" : "free", p.super_steer ? "on" : "off", p.avoid_props ? "on" : "off", p.catch_up ? "on" : "off", kOverlays[p.overlay]);
}

const char *control_name(int control) { return kControls[control]; }
const char *shortcuts_name(int shortcuts) { return kShortcuts[shortcuts]; }
const char *overlay_name(int overlay) { return kOverlays[overlay]; }

}  // namespace improved_ai_opponents::settings

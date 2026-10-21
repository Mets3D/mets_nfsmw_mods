#include "settings.h"

#include <cstdio>
#include <cstring>

#include "improved_ai_opponents.h"

namespace improved_ai_opponents::settings {

namespace {

Player g_players[2];

const char *const kControls[] = {"off", "vanilla", "improved"};
const char *const kNitros[] = {"disabled", "fair", "allowed"};
const char *const kShortcuts[] = {"avoid", "allowed", "always"};

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
        p.nitro = read_choice(section, "nitro", kNitros, d.nitro);
        p.shortcuts = read_choice(section, "shortcuts", kShortcuts, d.shortcuts);
        p.catch_up = GetPrivateProfileIntA(section, "catch_up", d.catch_up ? 1 : 0, g_ini) != 0;
        p.overlay = GetPrivateProfileIntA(section, "overlay", d.overlay ? 1 : 0, g_ini) != 0;
    }
}

void save(int who) {
    const char *section = section_of(who);
    const Player &p = g_players[who];
    char text[16];
    snprintf(text, sizeof(text), "%d", p.skill);
    WritePrivateProfileStringA(section, "control", kControls[p.control], g_ini);
    WritePrivateProfileStringA(section, "skill", text, g_ini);
    WritePrivateProfileStringA(section, "nitro", kNitros[p.nitro], g_ini);
    WritePrivateProfileStringA(section, "shortcuts", kShortcuts[p.shortcuts], g_ini);
    WritePrivateProfileStringA(section, "catch_up", p.catch_up ? "1" : "0", g_ini);
    WritePrivateProfileStringA(section, "overlay", p.overlay ? "1" : "0", g_ini);
    g_log.line("player %d's AI settings saved: control %s, skill %d, nitro %s, shortcuts %s, catch-up %s, overlay %s", who + 1, kControls[p.control], p.skill, kNitros[p.nitro],
               kShortcuts[p.shortcuts], p.catch_up ? "on" : "off", p.overlay ? "on" : "off");
}

const char *control_name(int control) { return kControls[control]; }
const char *nitro_name(int nitro) { return kNitros[nitro]; }
const char *shortcuts_name(int shortcuts) { return kShortcuts[shortcuts]; }

}  // namespace improved_ai_opponents::settings

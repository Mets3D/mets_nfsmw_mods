/*
 * Options > AI Driving (through nfsmw_custom_menus, see custom_menus_api.h): the AI settings of the player whose options are open (a page of the
 * pause menu; PlayerToEdit says whose). The settings are the live ones (settings.h): a change takes effect at once, Accept saves it to the ini,
 * Back asks before it is thrown away. Rows that cannot matter for the current choice are greyed out: the skill and catch-up need an AI at
 * the wheel, nitrous, shortcuts, the race route and super steer the improved AI.
 */
#include <algorithm>
#include <cstdio>

#include "custom_menus_api.h"
#include "improved_ai_opponents.h"
#include "settings.h"

namespace opt = nfsmw::custom_menus;
namespace st = improved_ai_opponents::settings;

namespace {

constexpr uintptr_t kPlayerToEdit = 0x91CA24;  // UIOptionsScreen::PlayerToEdit: whose options are open
constexpr unsigned kOnText = 0x417B2604, kOffText = 0x70DFE5C2;  // the game's own "On" and "Off"

enum Row { R_CONTROL, R_SKILL, R_NITRO, R_SHORTCUTS, R_RACE_ROUTE, R_SUPER_STEER, R_CATCHUP, R_OVERLAY };

constexpr opt::Row kRows[] = {
    {opt::kValueRow, "AI Control", 0,
     "Who drives your car. Off: you. Vanilla: the game's own racing AI, as it drives the opponents. Improved: the same AI with our changes (it gets out of a stuck "
     "position sooner, weighs the roads when it plans its route, and has the Nitro and Shortcuts options)."},
    {opt::kSliderRow, "Skill Level", 0, "How well the AI drives, from 0 to 100, for both the Vanilla and the Improved AI. Hold Shift or LB for finer steps."},
    {opt::kValueRow, "Nitro", 0,
     "Improved AI only. Disabled: it never uses nitrous. Fair: it uses nitrous as far as the human players have it (the game's own rule). Allowed: it may "
     "use it whatever the other players have."},
    {opt::kValueRow, "Shortcuts", 0,
     "Improved AI only. Avoid: it never plans a shortcut. Allowed: it plans a shortcut when it is the better route. Always: it prefers shortcuts."},
    {opt::kValueRow, "Follow Race Route", 0,
     "Improved AI only. On: like the game's AI, it is held to the race's own route: the road segments the race was set up with, wherever your car is. Off: it can be on "
     "any road, starting from the road the car is on, and its route search chooses from there. Takes effect at the next route plan (F9 plans it again)."},
    {opt::kValueRow, "Super Steer", 0,
     "Improved AI only. Allow arbitrary steering, like the vanilla AI. Disable to make this AI's steering controls match a human player's."},
    {opt::kValueRow, "Catch-Up", 0, "The game's own catch-up: when the AI is behind the other players it drives better and accelerates harder, as the opponents do."},
    {opt::kValueRow, "Debug Overlays", 0,
     "Lets the AI plan its route beside your car while you drive, so that F8 can show what it calculates in your picture (the route's segments and its "
     "steering target). Nothing is drawn unless F8 or F11 is on."},
};

st::Player g_opened[2];  // each player's settings when the page opened

int editing() { return std::clamp(*reinterpret_cast<int *>(kPlayerToEdit), 0, 1); }
st::Player &now() { return st::player(editing()); }

unsigned value(int row, char *out, unsigned size) {
    const st::Player &p = now();
    static const char *const kControls[] = {"Off", "Vanilla", "Improved"};
    static const char *const kNitros[] = {"Disabled", "Fair", "Allowed"};
    static const char *const kShortcuts[] = {"Avoid", "Allowed", "Always"};
    switch (row) {
        case R_CONTROL: snprintf(out, size, "%s", kControls[p.control]); return 0;
        case R_SKILL: snprintf(out, size, "%d", p.skill); return 0;
        case R_NITRO: snprintf(out, size, "%s", kNitros[p.nitro]); return 0;
        case R_SHORTCUTS: snprintf(out, size, "%s", kShortcuts[p.shortcuts]); return 0;
        case R_RACE_ROUTE: return p.follow_race_route ? kOnText : kOffText;
        case R_SUPER_STEER: return p.super_steer ? kOnText : kOffText;
        case R_CATCHUP: return p.catch_up ? kOnText : kOffText;
        default: return p.overlay ? kOnText : kOffText;
    }
}

float fill(int row) { return row == R_SKILL ? now().skill / 100.0f : 0.0f; }

int step(int current, int direction, int count) { return std::clamp(current + direction, 0, count - 1); }

void change(int row, int direction, bool fine) {
    st::Player &p = now();
    switch (row) {
        case R_CONTROL: p.control = step(p.control, direction, 3); break;
        case R_SKILL: p.skill = std::clamp(p.skill + direction * (fine ? 1 : 5), 0, 100); break;
        case R_NITRO: p.nitro = step(p.nitro, direction, 3); break;
        case R_SHORTCUTS: p.shortcuts = step(p.shortcuts, direction, 3); break;
        case R_RACE_ROUTE: p.follow_race_route = direction > 0; break;
        case R_SUPER_STEER: p.super_steer = direction > 0; break;
        case R_CATCHUP: p.catch_up = direction > 0; break;
        case R_OVERLAY: p.overlay = direction > 0; break;
    }
}

bool row_enabled(int row) {
    const st::Player &p = now();
    switch (row) {
        case R_SKILL:
        case R_CATCHUP: return p.control != st::kOff;
        case R_NITRO:
        case R_SHORTCUTS:
        case R_RACE_ROUTE:
        case R_SUPER_STEER: return p.control == st::kImproved;
        default: return true;
    }
}

bool same(const st::Player &a, const st::Player &b) {
    return a.control == b.control && a.skill == b.skill && a.nitro == b.nitro && a.shortcuts == b.shortcuts && a.follow_race_route == b.follow_race_route && a.super_steer == b.super_steer && a.catch_up == b.catch_up && a.overlay == b.overlay;
}

void opened() {
    for (int who = 0; who < 2; ++who) g_opened[who] = st::player(who);
}
bool available() { return improved_ai_opponents::g_active; }
bool changed() { return !same(now(), g_opened[editing()]); }
void accept() {
    for (int who = 0; who < 2; ++who) {
        if (same(st::player(who), g_opened[who])) continue;
        st::save(who);
        g_opened[who] = st::player(who);
    }
}
void discard() { now() = g_opened[editing()]; }
void defaults() { now() = st::defaults(editing()); }

opt::Page g_page;

}  // namespace

// Called by nfsmw_custom_menus when the Options menu first opens.
extern "C" __declspec(dllexport) void nfsmw_custom_menus_provide(const nfsmw::custom_menus::Host *host) {
    g_page = {"improved_ai_opponents", "AI Driving", "AI DRIVING", "AI DRIVING", 0, 0, nullptr, static_cast<int>(sizeof(kRows) / sizeof(kRows[0])), kRows, available, opened, value, change, changed,
              accept, discard, defaults, opt::kNewEntry, fill, row_enabled};
    host->register_page(&g_page);
}

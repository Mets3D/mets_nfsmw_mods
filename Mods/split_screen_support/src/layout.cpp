#include "layout.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "custom_menus_api.h"
#include "max_players.h"
#include "more_players.h"
#include "racers.h"
#include "split_screen_support.h"

namespace split_screen_support::layout {

namespace {

constexpr float kWaitSeconds = 3.0f;   // after a player is out (their "Totalled" or "Knocked Out" message), before their cell goes
constexpr float kSlideSeconds = 1.2f;  // the slide

bool g_vertical = false;
bool g_overtake = true;

// How much of its home size a cell has along its line, and a line across the lines: 1 its home, 0 none. Linear in time; eased when used.
int g_players = 0;  // of the race the shares are for
float g_share[nfsmw::kMaxPlayers], g_line_share[nfsmw::kMaxPlayers];
float g_out_for[nfsmw::kMaxPlayers];  // seconds since the player went out, -1 while they race
LARGE_INTEGER g_last = {};

bool is_out(int player) {
    const unsigned char *info = racers::info(racers::of_player(more_players::game_player(player)));
    return info && racers::out(info);
}

// The line of a player (a row, or a column when vertical), and the place in it.
int line_of(int player, int players) {
    if (players <= 2) return player;
    if (players == 3) return player == 0 ? 0 : 1;
    return g_vertical ? player % grid_size(players) : player / grid_size(players);
}

int place_in_line(int player, int players) {
    if (players <= 2) return 0;
    if (players == 3) return player == 0 ? 0 : player - 1;
    return g_vertical ? player / grid_size(players) : player % grid_size(players);
}

int line_count(int players) {
    if (players <= 3) return players == 1 ? 1 : 2;
    return grid_size(players);
}

// A cell from its span along its line and across the lines.
struct Span {
    long lo, hi;
};
Span along(const RECT &r) { return g_vertical ? Span{r.top, r.bottom} : Span{r.left, r.right}; }
Span across(const RECT &r) { return g_vertical ? Span{r.left, r.right} : Span{r.top, r.bottom}; }
RECT make(Span a, Span c) { return g_vertical ? RECT{c.lo, a.lo, c.hi, a.hi} : RECT{a.lo, c.lo, a.hi, c.hi}; }

void reset(int players) {
    g_players = players;
    for (int i = 0; i < nfsmw::kMaxPlayers; ++i) {
        g_share[i] = g_line_share[i] = 1.0f;
        g_out_for[i] = -1.0f;
    }
}

float eased(float t) { return t * t * (3.0f - 2.0f * t); }

void slide(float &share, float target, float step) { share = target < share ? std::max(target, share - step) : std::min(target, share + step); }

bool moved() {
    for (int i = 0; i < g_players; ++i)
        if (g_share[i] < 1.0f || g_line_share[i] < 1.0f) return true;
    return false;
}

}  // namespace

bool vertical() { return g_vertical; }
void set_vertical(bool on) { g_vertical = on; }

RECT home_cell(int player, int players, long width, long height) {
    if (players <= 1) return {0, 0, width, height};
    const long main = g_vertical ? height : width, cross = g_vertical ? width : height;
    const int line = line_of(player, players), lines = line_count(players), place = place_in_line(player, players);
    const int places = players == 2 ? 1 : players == 3 ? (player == 0 ? 1 : 2) : grid_size(players);
    return make({main * place / places, main * (place + 1) / places}, {cross * line / lines, cross * (line + 1) / lines});
}

void update(int players) {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    const float seconds = g_last.QuadPart ? std::clamp(static_cast<float>(now.QuadPart - g_last.QuadPart) / frequency.QuadPart, 0.0f, 0.1f) : 0.0f;
    g_last = now;
    players = std::min(players, nfsmw::kMaxPlayers);
    if (players != g_players || !g_overtake) return reset(players);

    bool out[nfsmw::kMaxPlayers] = {}, anybody_out = false, anybody_racing = false;
    for (int i = 0; i < players; ++i) {
        out[i] = is_out(i);
        anybody_out |= out[i];
        anybody_racing |= !out[i];
    }
    if (!anybody_out) return reset(players);  // (a new race, or the race restarted)
    if (!anybody_racing) return;               // the race is over: the screen stays as it is

    const int lines = line_count(players);
    bool gone[nfsmw::kMaxPlayers] = {}, line_racing[nfsmw::kMaxPlayers] = {}, line_gone[nfsmw::kMaxPlayers];
    std::fill(line_gone, line_gone + lines, true);
    for (int i = 0; i < players; ++i) {
        if (!out[i]) {
            g_out_for[i] = -1.0f;
        } else if (g_out_for[i] < 0.0f) {
            g_out_for[i] = 0.0f;
            g_log.line("layout: player %d is out; their cell goes to the others", i + 1);
        } else {
            g_out_for[i] += seconds;
        }
        gone[i] = out[i] && g_out_for[i] >= kWaitSeconds;
        line_racing[line_of(i, players)] |= !out[i];
        line_gone[line_of(i, players)] &= gone[i];
    }
    const float step = seconds / kSlideSeconds;
    for (int i = 0; i < players; ++i)
        if (line_racing[line_of(i, players)]) slide(g_share[i], gone[i] ? 0.0f : 1.0f, step);  // (a line with nobody racing keeps its cells while it closes)
    for (int l = 0; l < lines; ++l) slide(g_line_share[l], line_gone[l] ? 0.0f : 1.0f, step);  // (somebody races: some line stays)
}

RECT cell(int player, int players, long width, long height) {
    const RECT home = home_cell(player, players, width, height);
    if (players < 2 || players != g_players || !moved()) return home;
    const int line = line_of(player, players), lines = line_count(players);
    float before_line = 0.0f, all_lines = 0.0f;
    for (int l = 0; l < lines; ++l) {
        const float share = eased(g_line_share[l]);
        if (l < line) before_line += share;
        all_lines += share;
    }
    float before = 0.0f, total = 0.0f;
    long lo = g_vertical ? height : width, hi = 0, end = 0;  // the line's span along it; where the lines end across (a grid of 5 has an empty row)
    for (int i = 0; i < players; ++i) {
        const RECT other = home_cell(i, players, width, height);
        end = std::max(end, across(other).hi);
        if (line_of(i, players) != line) continue;
        const float share = eased(g_share[i]);
        if (i < player) before += share;
        total += share;
        lo = std::min(lo, along(other).lo);
        hi = std::max(hi, along(other).hi);
    }
    if (all_lines <= 0.0f || total <= 0.0f) return home;
    const float span = static_cast<float>(hi - lo), mine = eased(g_share[player]), thick = eased(g_line_share[line]);
    const Span a = {lo + std::lround(span * before / total), lo + std::lround(span * (before + mine) / total)};
    const Span c = {std::lround(end * before_line / all_lines), std::lround(end * (before_line + thick) / all_lines)};
    return make(a, c);
}

float hud_scale(int player, int players, long width, long height) {
    const RECT now = cell(player, players, width, height);
    return width > 0 ? std::min(1.0f, static_cast<float>(now.right - now.left) / static_cast<float>(width)) : 1.0f;
}

float mirror_scale(int player, int players, long width, long height) {
    const RECT now = cell(player, players, width, height);
    if (width <= 0 || height <= 0) return 1.0f;
    return std::min(hud_scale(player, players, width, height), static_cast<float>(now.bottom - now.top) / static_cast<float>(height));
}

float presence(int player) {
    if (player < 0 || player >= g_players) return 1.0f;
    return eased(g_share[player]) * eased(g_line_share[line_of(player, g_players)]);
}

bool game_halves(int players) { return players >= 2 && players <= 3 && !g_vertical && !(players == g_players && moved()); }

// ---- Options > Advanced Video: Split Screen Overtake (the ini's allow_split_screen_overtake, at once)
namespace {

namespace opt = nfsmw::custom_menus;

constexpr unsigned kOnText = 0x417B2604, kOffText = 0x70DFE5C2;  // the game's own "On" and "Off"
constexpr opt::Row kRows[] = {
    {opt::kValueRow, "Split Screen Overtake", 0,
     "When a player is knocked out, totalled or blows their engine, the remaining players take over their part of the screen."},
};
bool g_opened_overtake = true;

void opened() { g_opened_overtake = g_overtake; }
unsigned value(int, char *, unsigned) { return g_overtake ? kOnText : kOffText; }
void change(int, int direction, bool) { g_overtake = direction > 0; }
bool changed() { return g_overtake != g_opened_overtake; }
void accept() {
    WritePrivateProfileStringA("options", "allow_split_screen_overtake", g_overtake ? "1" : "0", g_ini);
    g_log.line("layout: split screen overtake %s", g_overtake ? "on" : "off");
    g_opened_overtake = g_overtake;
}
void discard() { g_overtake = g_opened_overtake; }
void defaults() { g_overtake = true; }
opt::Page g_page;

}  // namespace

void register_page(const nfsmw::custom_menus::Host *host) {
    g_page = {"split_overtake", "Split Screen Overtake", "SPLIT SCREEN OVERTAKE", "SPLIT SCREEN OVERTAKE", 0, 0, nullptr, 1, kRows, nullptr,
              opened, value, change, changed, accept, discard, defaults, opt::kAdvancedVideoOptions, nullptr, nullptr, nullptr};
    host->register_page(&g_page);
}

void install() { g_overtake = ini_flag("options", "allow_split_screen_overtake", true); }

}  // namespace split_screen_support::layout

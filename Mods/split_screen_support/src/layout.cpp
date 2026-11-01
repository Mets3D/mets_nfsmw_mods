#include "layout.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "max_players.h"
#include "more_players.h"
#include "racers.h"
#include "split_screen_support.h"

namespace split_screen_support::layout {

namespace {

constexpr float kWaitSeconds = 3.0f;   // after a player is out (their "Totalled" or "Knocked Out" message), before their cell goes
constexpr float kSlideSeconds = 1.2f;  // the slide

// How much of its home size a cell has in its row (its width), and a row among the rows (its height): 1 its home, 0 none. Linear in time; eased when used.
int g_players = 0;  // of the race the shares are for
float g_share[nfsmw::kMaxPlayers], g_row_share[nfsmw::kMaxPlayers];
float g_out_for[nfsmw::kMaxPlayers];  // seconds since the player went out, -1 while they race
LARGE_INTEGER g_last = {};

bool is_out(int player) {
    const unsigned char *info = racers::info(racers::of_player(more_players::game_player(player)));
    return info && racers::out(info);
}

int row_of(int player, int players) {
    if (players <= 2) return player;
    if (players == 3) return player == 0 ? 0 : 1;
    return player / grid_size(players);
}

int row_count(int players) { return row_of(players - 1, players) + 1; }

void reset(int players) {
    g_players = players;
    for (int i = 0; i < nfsmw::kMaxPlayers; ++i) {
        g_share[i] = g_row_share[i] = 1.0f;
        g_out_for[i] = -1.0f;
    }
}

float eased(float t) { return t * t * (3.0f - 2.0f * t); }

void slide(float &share, float target, float step) { share = target < share ? std::max(target, share - step) : std::min(target, share + step); }

bool moved() {
    for (int i = 0; i < g_players; ++i)
        if (g_share[i] < 1.0f || g_row_share[i] < 1.0f) return true;
    return false;
}

}  // namespace

void update(int players) {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    const float seconds = g_last.QuadPart ? std::clamp(static_cast<float>(now.QuadPart - g_last.QuadPart) / frequency.QuadPart, 0.0f, 0.1f) : 0.0f;
    g_last = now;
    players = std::min(players, nfsmw::kMaxPlayers);
    static const bool on = ini_flag("options", "allow_split_screen_overtake", true);
    if (players != g_players || !on) return reset(players);

    bool out[nfsmw::kMaxPlayers] = {}, anybody_out = false, anybody_racing = false;
    for (int i = 0; i < players; ++i) {
        out[i] = is_out(i);
        anybody_out |= out[i];
        anybody_racing |= !out[i];
    }
    if (!anybody_out) return reset(players);  // (a new race, or the race restarted)
    if (!anybody_racing) return;               // the race is over: the screen stays as it is

    const int rows = row_count(players);
    bool gone[nfsmw::kMaxPlayers] = {}, row_racing[nfsmw::kMaxPlayers] = {}, row_gone[nfsmw::kMaxPlayers];
    std::fill(row_gone, row_gone + rows, true);
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
        row_racing[row_of(i, players)] |= !out[i];
        row_gone[row_of(i, players)] &= gone[i];
    }
    const float step = seconds / kSlideSeconds;
    for (int i = 0; i < players; ++i)
        if (row_racing[row_of(i, players)]) slide(g_share[i], gone[i] ? 0.0f : 1.0f, step);  // (a row with nobody racing keeps its cells while it closes)
    for (int r = 0; r < rows; ++r) slide(g_row_share[r], row_gone[r] ? 0.0f : 1.0f, step);  // (somebody races: some row stays)
}

RECT cell(int player, int players, long width, long height) {
    const RECT home = home_cell(player, players, width, height);
    if (players < 2 || players != g_players || !moved()) return home;
    const int row = row_of(player, players), rows = row_count(players);
    float above = 0.0f, all = 0.0f;
    for (int r = 0; r < rows; ++r) {
        const float share = eased(g_row_share[r]);
        if (r < row) above += share;
        all += share;
    }
    float before = 0.0f, total = 0.0f;
    long left = width, right = 0;
    for (int i = 0; i < players; ++i) {
        if (row_of(i, players) != row) continue;
        const RECT other = home_cell(i, players, width, height);
        const float share = eased(g_share[i]);
        if (i < player) before += share;
        total += share;
        left = std::min(left, other.left);
        right = std::max(right, other.right);
    }
    if (all <= 0.0f || total <= 0.0f) return home;
    const float span = static_cast<float>(right - left), mine = eased(g_share[player]);
    const float bottom = static_cast<float>(home_cell(players - 1, players, width, height).bottom), tall = eased(g_row_share[row]);  // (the rows' bottom: a grid of 5 has an empty row)
    RECT r;
    r.left = left + std::lround(span * before / total);
    r.right = left + std::lround(span * (before + mine) / total);
    r.top = std::lround(bottom * above / all);
    r.bottom = std::lround(bottom * (above + tall) / all);
    return r;
}

float hud_scale(int player, int players, long width, long height) {
    const RECT home = home_cell(player, players, width, height), now = cell(player, players, width, height);
    const long home_width = home.right - home.left;
    if (home_width <= 0) return 1.0f;
    return std::min(1.0f, hud_base_scale(home, width, height) * (now.right - now.left) / home_width);
}

float presence(int player) {
    if (player < 0 || player >= g_players) return 1.0f;
    return eased(g_share[player]) * eased(g_row_share[row_of(player, g_players)]);
}

bool changed(int players) { return players >= 2 && players == g_players && moved(); }

}  // namespace split_screen_support::layout

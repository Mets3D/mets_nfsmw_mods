#include "look_options.h"

#include <algorithm>

#include "max_players.h"
#include "pad.h"
#include "split_screen_api.h"

namespace pad::look_options {

namespace opt = nfsmw::custom_menus;

namespace {

constexpr uintptr_t kPlayerToEdit = 0x91CA24;  // UIOptionsScreen::PlayerToEdit: whose Player options are open
constexpr unsigned kOffText = 0x70DFE5C2;      // the game's own "Off"
constexpr int kMaxSeconds = 10;

constexpr int kPlayers = nfsmw::kMaxPlayers;
float g_delay[kPlayers];   // seconds, 0 = never (3 until load)
float g_opened[kPlayers];  // what was last accepted (and is in the ini)

const char *section_of(int who) {
    static char name[kPlayers][16];
    if (!name[who][0]) snprintf(name[who], sizeof(name[who]), "look_player_%d", who + 1);
    return name[who];
}
// Whose options are open: with split screen support, the player who asked for the pause menu (0 to 3), otherwise the game's own number (0 or 1).
int editing() {
    static nfsmw::split_screen_api::OptionsPlayerFn options_player = nullptr;
    if (!options_player) options_player = nfsmw::split_screen_api::find_options_player();  // the mods load in any order: looked up until found
    const int player = options_player ? options_player() : -1;
    return player >= 0 && player < kPlayers ? player : std::clamp(*reinterpret_cast<int *>(kPlayerToEdit), 0, 1);
}

void save(int who) {
    char text[32];
    snprintf(text, sizeof(text), "%g", g_delay[who]);
    WritePrivateProfileStringA(section_of(who), "return_delay", text, g_ini);
}

// ---- the row ----
constexpr opt::Row kRows[] = {
    {opt::kValueRow, "Free Look Reset", 0,
     "How long the camera waits after you stop looking around before it returns behind the car. Off leaves it where you put it."},
};

unsigned value(int, char *out, unsigned size) {
    const float seconds = g_delay[editing()];
    if (seconds <= 0.0f) return kOffText;
    snprintf(out, size, "%g s", seconds);
    return 0;
}

void change(int, int direction, bool) {
    float &seconds = g_delay[editing()];
    const int now = seconds <= 0.0f ? 0 : std::clamp(static_cast<int>(lroundf(seconds)), 1, kMaxSeconds);
    seconds = static_cast<float>(std::clamp(now + direction, 0, kMaxSeconds));
}

void opened() { std::copy(g_delay, g_delay + kPlayers, g_opened); }
bool changed() { return g_delay[editing()] != g_opened[editing()]; }
void accept() {
    for (int who = 0; who < kPlayers; ++who) {
        if (g_delay[who] == g_opened[who]) continue;
        save(who);
        log_line("look: player %d free look returns after %g s (0 = never)", who + 1, g_delay[who]);
        g_opened[who] = g_delay[who];
    }
}
void discard() { g_delay[editing()] = g_opened[editing()]; }
void defaults() { g_delay[editing()] = std::max(0.0f, g_look_cfg.return_delay); }
bool available() { return g_look_cfg.enabled; }

opt::Page g_page;

}  // namespace

void load() {
    for (int who = 0; who < kPlayers; ++who)
        g_delay[who] = std::max(0.0f, ini_float("return_delay", g_look_cfg.return_delay, section_of(who)));
}

float return_delay(int who) { return g_delay[std::clamp(who, 0, kPlayers - 1)]; }

void provide(const opt::Host *host) {
    g_page = {"free_look", "Free Look", "FREE LOOK", "FREE LOOK", 0, 0, nullptr, 1, kRows, available, opened, value, change, changed, accept,
              discard, defaults, opt::kPlayerOptions};
    host->register_page(&g_page);
}

}  // namespace pad::look_options

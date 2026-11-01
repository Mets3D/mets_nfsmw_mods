/*
 * A "Players" row (2 up to [options] max_players, 4 by default) in the race options that follow the track select (UIQRTrackOptions, Track_Options.fng) of a split screen
 * race. The screen builds its rows in Setup (0x7BA7C0), which calls one of SetupCircuit / SetupSprint ... and sets the first row as the
 * cursor's; after it returns our row is added (option_row.h). The row's texts are language strings registered through nfsmw_custom_menus (without it
 * there is no row, and the race has two players).
 * Left and right change the value: 2 up to max_players (an odd count has a last round of Press Start / Car Select for one player: see more_players.cpp).
 * Under it a "Layout" row: Horizontal (the game's: the players' pictures in rows) or Vertical (in columns; layout.h), kept in the ini ([race_options] layout).
 */
#include "player_count.h"

#include <algorithm>
#include <cstring>

#include "ai_in_split_screen.h"
#include "hooks.h"
#include "layout.h"
#include "max_players.h"
#include "more_players.h"
#include "option_row.h"
#include "patch.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::player_count {

namespace {

const unsigned kTitleHash = nfsmw::bstring_hash("SPLIT_PLAYERS");
// The most players the row offers ([options] max_players, 4 by default: the most that has been played; up to nfsmw::kMaxPlayers).
int max_players() {
    static const int value = std::clamp(static_cast<int>(GetPrivateProfileIntA("options", "max_players", 4, g_ini)), 2, nfsmw::kMaxPlayers);
    return value;
}

// The language string of the row's value for n players.
unsigned value_hash(int players) {
    char name[32];
    snprintf(name, sizeof(name), "SPLIT_PLAYERS_%d", players);
    return nfsmw::bstring_hash(name);
}

int g_wanted = 2;
bool g_texts = false;
void *g_vtable[option_row::kSlots];

void __fastcall draw(void *self, void *) { option_row::set_texts(self, kTitleHash, value_hash(g_wanted)); }

const unsigned kLayoutTitleHash = nfsmw::bstring_hash("SPLIT_LAYOUT"), kHorizontalHash = nfsmw::bstring_hash("SPLIT_LAYOUT_HORIZONTAL"),
               kVerticalHash = nfsmw::bstring_hash("SPLIT_LAYOUT_VERTICAL");
void *g_layout_vtable[option_row::kSlots];

void __fastcall layout_draw(void *self, void *) { option_row::set_texts(self, kLayoutTitleHash, layout::vertical() ? kVerticalHash : kHorizontalHash); }

void __fastcall layout_act(void *self, void *, const char *, unsigned data) {
    if (data != option_row::kPadLeft && data != option_row::kPadRight) return;
    layout::set_vertical(!layout::vertical());
    WritePrivateProfileStringA("race_options", "layout", layout::vertical() ? "vertical" : "horizontal", g_ini);
    option_row::value_changed(self, data);
    g_log.line("race options: layout %s", layout::vertical() ? "vertical" : "horizontal");
}

void __fastcall act(void *self, void *, const char *, unsigned data) {
    if (data != option_row::kPadLeft && data != option_row::kPadRight) return;
    g_wanted = data == option_row::kPadRight ? (g_wanted >= max_players() ? 2 : g_wanted + 1) : (g_wanted <= 2 ? max_players() : g_wanted - 1);
    option_row::value_changed(self, data);
    g_log.line("race options: %d players", g_wanted);
    ai_in_split_screen::players_changed();
}

constexpr uintptr_t kSetup = 0x7BA7C0;          // UIQRTrackOptions::Setup
constexpr uintptr_t kSetupCallSite = 0x7BF73D;  // its only call, in the screen's constructor (0x7BF6C0)

using SetupFn = void(__fastcall *)(void *, void *);

uintptr_t g_setup = kSetup;  // what the constructor called before the redirect: Setup, or another mod's function that calls it

// What the constructor calls instead.
void __fastcall setup_and_row(void *screen, void *edx) {
    reinterpret_cast<SetupFn>(g_setup)(screen, edx);
    more_players::reset();  // a new race setup: the next race starts with the first round of players
    g_log.line("race options built: split screen %d, texts %d", players::split_screen(), g_texts);
    if (!players::split_screen() || !g_texts) return;
    option_row::add(screen, g_vtable);
    option_row::add(screen, g_layout_vtable);
    option_row::refresh(screen);
    g_log.line("race options: the Players and Layout rows were added");
}

}  // namespace

int wanted() { return g_wanted; }

void register_texts(const nfsmw::custom_menus::Host *host) {
    host->register_text(kTitleHash, "Players");
    host->register_text(kLayoutTitleHash, "Layout");
    host->register_text(kHorizontalHash, "Horizontal");
    host->register_text(kVerticalHash, "Vertical");
    static char texts[nfsmw::kMaxPlayers + 1][8];  // the host keeps the pointers
    for (int players = 2; players <= max_players(); ++players) {
        snprintf(texts[players], sizeof(texts[players]), "%d", players);
        host->register_text(value_hash(players), texts[players]);
    }
    g_texts = true;
}

void install() {
    option_row::make_vtable(g_vtable, &act, &draw);
    option_row::make_vtable(g_layout_vtable, &layout_act, &layout_draw);
    char layout_name[16];
    GetPrivateProfileStringA("race_options", "layout", "horizontal", layout_name, sizeof(layout_name), g_ini);
    layout::set_vertical(_stricmp(layout_name, "vertical") == 0);
    // The call is redirected, not the function hooked: other mods change functions too, and a hook on Setup was never reached.
    if (const uintptr_t previous = nfsmw::chain_call(kSetupCallSite, reinterpret_cast<uintptr_t>(&setup_and_row))) {
        g_setup = previous;
        g_log.line("race options: the Players row will be added (the constructor called %#x%s)", static_cast<unsigned>(previous),
                   previous == kSetup ? ", Setup" : ", not Setup: another mod redirected it");
    } else {
        g_log.line("race options: the call at %#x is not a call; no Players row", static_cast<unsigned>(kSetupCallSite));
    }
}

}  // namespace split_screen_support::player_count

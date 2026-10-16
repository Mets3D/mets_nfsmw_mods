/*
 * A row in Options > Player (through nfsmw_custom_menus, see custom_menus_api.h): Minimap On / Off, for the player whose options are
 * open. It works in a single player game as well as in split screen, for player 1 and player 2.
 *
 * Which HUD elements a player's HUD shows is decided by FEngHud::DetermineHudFeatures (0x57CA60): a mask of bits, 0x10000 for the
 * minimap and 0x4000 for the heat meter around it, which FEngHud::Update turns into HudElement::Toggle calls. The game's own minimap setting (RacingMiniMapMode in the gameplay
 * settings, 2 = off) is not per player; this takes the bit out for one player. The second player's minimap (minimap.cpp) asks the same
 * function for its visibility, so it follows too.
 *
 * Each player's choice is kept in the ini ([player_1] / [player_2] minimap=0).
 */
#include "minimap_option.h"

#include <algorithm>

#include "custom_menus_api.h"
#include "hooks.h"
#include "split_screen_support.h"

namespace split_screen_support::minimap_option {

namespace opt = nfsmw::custom_menus;

namespace {

constexpr uintptr_t kDetermineHudFeatures = 0x57CA60;
constexpr uintptr_t kPlayerToEdit = 0x91CA24;  // UIOptionsScreen::PlayerToEdit: whose Player options are open
constexpr int kHudType = 0x20;                 // FEngHud: ePlayerHudType
constexpr int kHudSplit2 = 4;                  // player 2's HUD in split screen; every other type is player 1's
// The minimap (bit 16) and the heat meter (bit 14, the flame and the ring around the minimap): the game sets and clears them together
// (its own minimap setting off takes both out).
constexpr unsigned long long kMinimapFeatures = 0x10000 | 0x4000;
constexpr unsigned kOnText = 0x417B2604, kOffText = 0x70DFE5C2;  // the game's own "On" and "Off"

bool g_show[2] = {true, true};
bool g_opened[2];

const char *section_of(int who) { return who == 0 ? "player_1" : "player_2"; }
int editing() { return std::clamp(*reinterpret_cast<int *>(kPlayerToEdit), 0, 1); }

// ---- the hook
using FeaturesFn = unsigned long long(__fastcall *)(void *, void *, void *);
FeaturesFn g_orig_features = nullptr;

unsigned long long __fastcall hook_features(void *self, void *edx, void *player) {
    unsigned long long features = g_orig_features(self, edx, player);
    const int who = *reinterpret_cast<int *>(static_cast<char *>(self) + kHudType) == kHudSplit2 ? 1 : 0;
    if (!g_show[who]) features &= ~kMinimapFeatures;
    return features;
}

// ---- the row
constexpr opt::Row kRows[] = {
    {opt::kValueRow, "Minimap", 0, "Shows or hides your minimap while you drive."},
};

unsigned value(int, char *, unsigned) { return g_show[editing()] ? kOnText : kOffText; }
void change(int, int direction, bool) { g_show[editing()] = direction > 0; }
void opened() {
    g_opened[0] = g_show[0];
    g_opened[1] = g_show[1];
}
bool changed() { return g_show[editing()] != g_opened[editing()]; }
void accept() {
    for (int who = 0; who < 2; ++who) {
        if (g_show[who] == g_opened[who]) continue;
        WritePrivateProfileStringA(section_of(who), "minimap", g_show[who] ? "1" : "0", g_ini);
        g_log.line("player %d's minimap %s", who + 1, g_show[who] ? "shown" : "hidden");
        g_opened[who] = g_show[who];
    }
}
void discard() { g_show[editing()] = g_opened[editing()]; }
void defaults() { g_show[editing()] = true; }

opt::Page g_page;

}  // namespace

void install() {
    for (int who = 0; who < 2; ++who) g_show[who] = GetPrivateProfileIntA(section_of(who), "minimap", 1, g_ini) != 0;
    const nfsmw::Hook hooks[] = {
        {"hudfeatures", "FEngHud::DetermineHudFeatures", kDetermineHudFeatures, {0x53, 0x55, 0x56, 0x8B, 0xD9, 0x8B, 0x4C}, false,
         nfsmw::detour(&hook_features), nfsmw::orig(&g_orig_features)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "minimap option");
}

}  // namespace split_screen_support::minimap_option

// Called by nfsmw_custom_menus when the Options menu first opens.
extern "C" __declspec(dllexport) void nfsmw_custom_menus_provide(const nfsmw::custom_menus::Host *host) {
    using namespace split_screen_support::minimap_option;
    char translations[MAX_PATH];
    nfsmw::data_file("translations\\split_screen_support", translations, sizeof(translations));
    host->register_translations(translations);  // before any text is registered
    g_page = {"split_minimap", "Minimap", "MINIMAP", "MINIMAP", 0, 0, nullptr, 1, kRows, nullptr, opened, value, change, changed, accept,
              discard, defaults, opt::kPlayerOptions};
    host->register_page(&g_page);
}

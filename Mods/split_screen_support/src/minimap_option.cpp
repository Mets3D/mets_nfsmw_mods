/*
 * A row in Options > Player (through nfsmw_custom_menus, see custom_menus_api.h): Minimap On / Off, for the player whose options are
 * open. It works in a single player game as well as in split screen, for player 1 and player 2.
 *
 * Which HUD elements a player's HUD shows is decided by FEngHud::DetermineHudFeatures (0x57CA60): a mask of bits, 0x10000 for the
 * minimap and 0x4000 for the heat meter around it, which FEngHud::Update turns into HudElement::Toggle calls. The game's own minimap setting (RacingMiniMapMode in the gameplay
 * settings, 2 = off) is not per player; this takes the bit out for one player. The second player's minimap (minimap.cpp) asks the same
 * function for its visibility, so it follows too.
 *
 * The page also has the HUD Scale (how big the player's HUD is: hud_views.cpp). Each player's choices are kept in the ini ([player_1] ... [player_4] minimap=0,
 * hud_scale=70). With more than two players the player is the one whose HUD asked for the pause menu (hud_players.cpp), since the game's own numbers only go to 2.
 */
#include "minimap_option.h"

#include <algorithm>

#include "custom_menus_api.h"
#include "hooks.h"
#include "hud_players.h"
#include "hud_views.h"
#include "max_players.h"
#include "ai_in_split_screen.h"
#include "player_count.h"
#include "player_settings.h"
#include "split_screen_support.h"

namespace split_screen_support::minimap_option {

namespace opt = nfsmw::custom_menus;

namespace {

constexpr uintptr_t kDetermineHudFeatures = 0x57CA60;
constexpr uintptr_t kPlayerToEdit = 0x91CA24;  // UIOptionsScreen::PlayerToEdit: whose Player options are open
constexpr int kHudType = 0x20, kHudSettingsSlot = 0x2C;                 // FEngHud: ePlayerHudType
constexpr int kHudSplit2 = 4;                  // player 2's HUD in split screen; every other type is player 1's
// The minimap (bit 16) and the heat meter (bit 14, the flame and the ring around the minimap): the game sets and clears them together
// (its own minimap setting off takes both out).
constexpr unsigned long long kMinimapFeatures = 0x10000 | 0x4000;
constexpr unsigned kOnText = 0x417B2604, kOffText = 0x70DFE5C2;  // the game's own "On" and "Off"

constexpr int kPlayers = nfsmw::kMaxPlayers;
bool g_show[kPlayers];  // (true until load)
bool g_opened[kPlayers];
constexpr int kScaleStep = 10, kMinScale = 40;      // the HUD scale, percent: 40 to 100
int g_scale[kPlayers];  // (100 until load)
int g_scale_opened[kPlayers];

const char *section_of(int who) {
    static char name[kPlayers][16];
    if (!name[who][0]) snprintf(name[who], sizeof(name[who]), "player_%d", who + 1);
    return name[who];
}
// Whose options are open: the player who asked for the pause menu (with more than two players the game's PlayerToEdit only has 0 and 1).
int editing() {
    const int requester = hud_players::pause_requester();
    return requester >= 0 && requester < kPlayers ? requester : std::clamp(*reinterpret_cast<int *>(kPlayerToEdit), 0, 1);
}

// ---- the hook
using FeaturesFn = unsigned long long(__fastcall *)(void *, void *, void *);
FeaturesFn g_orig_features = nullptr;

unsigned long long __fastcall hook_features(void *self, void *edx, void *player) {
    int who = hud_players::player_of_hud(self);
    if (who < 0) who = *reinterpret_cast<int *>(static_cast<char *>(self) + kHudType) == kHudSplit2 ? 1 : 0;
    player_settings::Use settings(who, *reinterpret_cast<int *>(static_cast<char *>(self) + kHudSettingsSlot));  // the game reads the HUD toggles by the HUD's settings slot
    unsigned long long features = g_orig_features(self, edx, player);
    if (!g_show[who]) features &= ~kMinimapFeatures;
    return features;
}

// ---- the row
constexpr int kRowMinimap = 0, kRowScale = 1;
constexpr opt::Row kRows[] = {
    {opt::kValueRow, "Minimap", 0, "Shows or hides your minimap while you drive."},
    {opt::kValueRow, "HUD Scale", 0, "How big your HUD is: each corner of it keeps its place and gets smaller."},
};

unsigned value(int row, char *out, unsigned out_size) {
    if (row == kRowMinimap) return g_show[editing()] ? kOnText : kOffText;
    snprintf(out, out_size, "%d%%", g_scale[editing()]);
    return 0;
}
void change(int row, int direction, bool) {
    if (row == kRowMinimap) {
        g_show[editing()] = direction > 0;
        return;
    }
    int &scale = g_scale[editing()];
    scale = std::clamp(scale + (direction > 0 ? kScaleStep : -kScaleStep), kMinScale, 100);
    hud_views::set_scale(editing(), scale);  // at once, to be seen behind the menu
}
void opened() {
    for (int who = 0; who < kPlayers; ++who) {
        g_opened[who] = g_show[who];
        g_scale_opened[who] = g_scale[who];
    }
}
bool changed() { return g_show[editing()] != g_opened[editing()] || g_scale[editing()] != g_scale_opened[editing()]; }
void accept() {
    for (int who = 0; who < kPlayers; ++who) {
        if (g_show[who] != g_opened[who]) {
            WritePrivateProfileStringA(section_of(who), "minimap", g_show[who] ? "1" : "0", g_ini);
            g_log.line("player %d's minimap %s", who + 1, g_show[who] ? "shown" : "hidden");
            g_opened[who] = g_show[who];
        }
        if (g_scale[who] != g_scale_opened[who]) {
            char text[16];
            snprintf(text, sizeof(text), "%d", g_scale[who]);
            WritePrivateProfileStringA(section_of(who), "hud_scale", text, g_ini);
            g_log.line("player %d's HUD scale %d%%", who + 1, g_scale[who]);
            g_scale_opened[who] = g_scale[who];
        }
    }
}
void discard() {
    g_show[editing()] = g_opened[editing()];
    g_scale[editing()] = g_scale_opened[editing()];
    hud_views::set_scale(editing(), g_scale[editing()]);
}
void defaults() {
    g_show[editing()] = true;
    g_scale[editing()] = 100;
    hud_views::set_scale(editing(), 100);
}
opt::Page g_page;

}  // namespace

void install() {
    for (int who = 0; who < kPlayers; ++who) {
        g_scale[who] = std::clamp(static_cast<int>(GetPrivateProfileIntA(section_of(who), "hud_scale", 100, g_ini)), kMinScale, 100);
        hud_views::set_scale(who, g_scale[who]);
    }
    for (int who = 0; who < kPlayers; ++who) g_show[who] = GetPrivateProfileIntA(section_of(who), "minimap", 1, g_ini) != 0;
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
    g_page = {"split_minimap", "Minimap", "MINIMAP", "MINIMAP", 0, 0, nullptr, 2, kRows, nullptr, opened, value, change, changed, accept,
              discard, defaults, opt::kPlayerOptions, nullptr, nullptr};
    host->register_page(&g_page);
    split_screen_support::player_count::register_texts(host);
    split_screen_support::ai_in_split_screen::register_texts(host);
}

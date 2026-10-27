/*
 * The game keeps PlayerSettings for players 1 and 2 only (array of 0x14 bytes at [FEDatabase + 0x10] + 0x80), and the code that reads or edits them
 * indexes that array: the HUD (FEngHud::DetermineHudFeatures reads the toggles of Options > Player by the HUD's number, +0x2C: 0 or 1) and the widgets of
 * Options > Player (by UIOptionsScreen's PlayerToEdit: 0 or 1, inline in each widget). Players 3 and 4 have copies (more_players.cpp: give_own_settings)
 * that the car's code reaches through the IPlayer, so these two did not see them: the HUD of player 3 showed player 2's toggles, and Options > Player
 * edited player 1's or 2's.
 * The extra players' HUDs and pause requests carry the settings slot of the first two players (0 for player 3, 1 for player 4: the slot their LocalPlayer was made with).
 * - The HUD of an extra player is drawn with their copy in the place of that slot (Use, around DetermineHudFeatures).
 * - When Options opens for an extra player (the pause menu's requester, hud_players.cpp), their copy and the entry of the slot (PlayerToEdit) trade places until the
 *   last options screen is gone, so the widgets edit the copy (and the HUDs, through Use, still get each player's own). UIOptionsScreen's constructor and
 *   destructor bracket that.
 */
#include "player_settings.h"

#include <algorithm>

#include "events.h"
#include "hooks.h"
#include "max_players.h"
#include "hud_players.h"
#include "more_players.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::player_settings {

namespace {

constexpr uintptr_t kFeDatabase = 0x91CF90, kPlayerToEdit = 0x91CA24, kUIOptionsScreenConstructor = 0x54FD60, kUIOptionsScreenDestructor = 0x529280;
constexpr unsigned kSettingsArray = 0x80, kSettingsSize = 0x14;

int g_editing = -1;  // the extra player (0 or 1) whose settings are in the entry of g_slot while Options is open
int g_slot = 0;
int g_depth = 0;     // options screens alive

unsigned char *entry(int player) {
    unsigned char *database = *reinterpret_cast<unsigned char **>(kFeDatabase);
    return database ? *reinterpret_cast<unsigned char **>(database + 0x10) + kSettingsArray + player * kSettingsSize : nullptr;
}

void swap_bytes(unsigned char *a, unsigned char *b) {
    for (unsigned i = 0; i < kSettingsSize; ++i) std::swap(a[i], b[i]);
}

// Where the settings of player `who` are now.
unsigned char *location(int who) {
    if (g_editing >= 0) {
        if (who == g_slot) return more_players::extra_settings(g_editing);
        if (who == nfsmw::kPlayersPerRound + g_editing) return entry(g_slot);
    }
    return who <= 1 ? entry(who) : more_players::extra_settings(who - 2);
}

const char *section_of(int extra) {
    static char name[nfsmw::kMaxExtras][16];
    if (!name[extra][0]) snprintf(name[extra], sizeof(name[extra]), "player_%d", extra + nfsmw::kPlayersPerRound + 1);
    return name[extra];
}

void save(int extra, const unsigned char *settings) {
    char text[2 * kSettingsSize + 1];
    for (unsigned i = 0; i < kSettingsSize; ++i) snprintf(text + 2 * i, 3, "%02X", settings[i]);
    char old[2 * kSettingsSize + 1] = "";
    GetPrivateProfileStringA(section_of(extra), "settings", "", old, sizeof(old), g_ini);
    if (strcmp(old, text) == 0) return;
    WritePrivateProfileStringA(section_of(extra), "settings", text, g_ini);
    g_log.line("player settings: player %d's settings kept (%s)", extra + 3, text);
}

void release() {
    if (g_editing < 0) return;
    if (unsigned char *copy = more_players::extra_settings(g_editing)) {
        swap_bytes(entry(g_slot), copy);
        save(g_editing, copy);  // what the player chose in the options
    }
    g_editing = -1;
}

void *(__fastcall *g_orig_constructor)(void *, void *, void *);
void *__fastcall hook_constructor(void *self, void *edx, void *data) {
    if (g_depth++ == 0) {
        const int requester = hud_players::pause_requester();
        unsigned char *copy = requester >= 2 ? more_players::extra_settings(requester - 2) : nullptr;
        const int slot = *reinterpret_cast<int *>(kPlayerToEdit);
        if (copy && (slot == 0 || slot == 1) && entry(slot)) {
            swap_bytes(entry(slot), copy);
            g_editing = requester - 2;
            g_slot = slot;
            g_log.line("player settings: the options are player %d's (settings slot %d)", requester + 1, slot);
        }
    }
    return g_orig_constructor(self, edx, data);
}

void(__fastcall *g_orig_destructor)(void *, void *);
void __fastcall hook_destructor(void *self, void *edx) {
    g_orig_destructor(self, edx);
    if (g_depth > 0 && --g_depth == 0) release();
}

// The pause menu's Options > Player says "Player 1" under its title whoever opened it: the subtitle is the player's name and device. UIOptionsScreen::Setup
// (0x545C50) runs for every page; TogglePlayer (0x52A380), which sets the text, is called from it. The subtitle is the object 0x53BF826D of the page's package,
// a group of strings whose language hash (+0x60) is "Player 1" or "Player 2" (0x7B070984 / 5) on the Player page.
constexpr uintptr_t kUIOptionsScreenSetup = 0x545C50, kFEngFindObject = 0x524850, kFEWideStringAssign = 0x57E8C0;
constexpr unsigned kSubtitleObject = 0x53BF826D, kPlayerOneText = 0x7B070984, kPlayerTwoText = 0x7B070985, kScreenPackage = 0x10, kLabelHash = 0x60;

void(__fastcall *g_orig_setup)(void *, void *);
void __fastcall hook_setup(void *self, void *edx) {
    g_orig_setup(self, edx);
    const int player = hud_players::pause_requester();
    if (player < 0 || !players::split_screen()) return;
    const char *package = *reinterpret_cast<const char **>(static_cast<char *>(self) + kScreenPackage);
    char *group = package ? static_cast<char *>(reinterpret_cast<void *(*)(const char *, unsigned)>(kFEngFindObject)(package, kSubtitleObject)) : nullptr;
    if (!group) return;
    char text[160];
    more_players::player_name(player, text, sizeof(text));
    // The subtitle is a group of strings (the text and its shadow), as on the Car Select screen.
    for (void *child = *reinterpret_cast<void **>(group + 0x64); child; child = *reinterpret_cast<void **>(static_cast<char *>(child) + 4)) {
        char *string = static_cast<char *>(child);
        if (*reinterpret_cast<int *>(string + 0x18) != 2 || (*reinterpret_cast<unsigned *>(string + kLabelHash) | 1u) != kPlayerTwoText) continue;
        reinterpret_cast<void *(__fastcall *)(void *, void *, const char *)>(kFEWideStringAssign)(string + 0x64, nullptr, text);
        *reinterpret_cast<unsigned *>(string + 0x1C) |= (1u << 25) | (1u << 24) | 0x400002u;
    }
}

// A new race: nothing of the last one is left open.
void on_world_load() {
    g_depth = 0;
    release();
}

}  // namespace

bool load(int extra, unsigned char *out) {
    char text[2 * kSettingsSize + 1] = "";
    GetPrivateProfileStringA(section_of(extra), "settings", "", text, sizeof(text), g_ini);
    if (strlen(text) != 2 * kSettingsSize) return false;
    unsigned char bytes[kSettingsSize];
    for (unsigned i = 0; i < kSettingsSize; ++i) {
        unsigned value = 0;
        if (sscanf(text + 2 * i, "%2x", &value) != 1) return false;
        bytes[i] = static_cast<unsigned char>(value);
    }
    memcpy(out, bytes, kSettingsSize);
    return true;
}

Use::Use(int who, int slot_index) {
    if (who < 0 || who > 3 || slot_index < 0 || slot_index > 1) return;
    unsigned char *where = location(who), *slot = entry(slot_index);
    if (!where || !slot || where == slot) return;
    swap_bytes(where, slot);
    a_ = where;
    b_ = slot;
}

Use::~Use() {
    if (a_) swap_bytes(a_, b_);
}

void install() {
    events::on_world_load(&on_world_load);
    const nfsmw::Hook hooks[] = {
        {"optionsscreen", "UIOptionsScreen::UIOptionsScreen", kUIOptionsScreenConstructor, {0x6A, 0xFF, 0x68, 0xF8, 0x07, 0x87, 0x00}, false,
         nfsmw::detour(&hook_constructor), nfsmw::orig(&g_orig_constructor)},
        {"optionssetup", "UIOptionsScreen::Setup", kUIOptionsScreenSetup, {0x56, 0x8B, 0xF1, 0xE8, 0x38, 0x30, 0x04}, false, nfsmw::detour(&hook_setup),
         nfsmw::orig(&g_orig_setup)},
        {"optionsscreen_end", "UIOptionsScreen::~UIOptionsScreen", kUIOptionsScreenDestructor, {0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x30, 0x01}, false,
         nfsmw::detour(&hook_destructor), nfsmw::orig(&g_orig_destructor)},
    };
    nfsmw::install_hooks(hooks, 3, g_log, g_ini, "player settings");
}

}  // namespace split_screen_support::player_settings

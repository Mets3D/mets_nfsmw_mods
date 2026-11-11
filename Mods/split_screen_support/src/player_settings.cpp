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
#include <cctype>
#include <climits>
#include <cstddef>
#include <cstdlib>

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

// The PC PlayerSettings layout. Only settings a player can change in the PC menus are persisted for players 3 and 4. The other bytes inherit player 1's or 2's
// values when a race makes the extra player. This also deliberately leaves the final padding byte out of the ini.
struct Settings {
    unsigned char gauges, position, lap_info, score, vibration, leaderboard, transmission_prompt, drive_with_analog;
    int controller_config;
    int camera;
    unsigned char split_time, transmission, handling, padding;
};
static_assert(sizeof(Settings) == kSettingsSize);
static_assert(offsetof(Settings, camera) == 0x0C);

constexpr const char *kCameraNames[] = {"bumper", "hood", "close", "far", "super_far", "drift", "pursuit"};
constexpr int kCameraCount = sizeof(kCameraNames) / sizeof(kCameraNames[0]);
constexpr int kControllerConfigurations = 5;

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

void write_value(const char *section, const char *key, const char *value) {
    char old[32] = "";
    GetPrivateProfileStringA(section, key, "", old, sizeof(old), g_ini);
    if (strcmp(old, value) != 0) WritePrivateProfileStringA(section, key, value, g_ini);
}

void save(int extra, const unsigned char *settings) {
    Settings values;
    memcpy(&values, settings, sizeof(values));
    const char *section = section_of(extra);
    write_value(section, "transmission", values.transmission == 1 ? "manual" : "automatic");
    char camera[16];
    if (values.camera >= 0 && values.camera < kCameraCount) {
        snprintf(camera, sizeof(camera), "%s", kCameraNames[values.camera]);
    } else if (values.camera >= 0) {
        snprintf(camera, sizeof(camera), "%d", values.camera);
    } else {
        snprintf(camera, sizeof(camera), "close");
    }
    write_value(section, "camera", camera);
    write_value(section, "gauges", values.gauges ? "1" : "0");
    write_value(section, "position", values.position ? "1" : "0");
    write_value(section, "split_time", values.split_time == 4 ? "0" : "1");
    write_value(section, "score", values.score ? "1" : "0");
    write_value(section, "leaderboard", values.leaderboard ? "1" : "0");
    write_value(section, "vibration", values.vibration ? "1" : "0");
    int config = values.controller_config >= 0 && values.controller_config < kControllerConfigurations ? values.controller_config : 0;
    config += values.drive_with_analog ? 1 : kControllerConfigurations + 1;  // the number shown by Options > Controls
    char text[4];
    snprintf(text, sizeof(text), "%d", config);
    write_value(section, "controller_config", text);
    WritePrivateProfileStringA(section, "settings", nullptr, g_ini);  // remove the old raw-memory format, if present
    g_log.line("player settings: player %d's settings kept as named ini values", extra + 3);
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

bool read_text(int extra, const char *key, char *text, unsigned size) {
    GetPrivateProfileStringA(section_of(extra), key, "", text, size, g_ini);
    return text[0] != '\0';
}

void invalid_value(int extra, const char *key, const char *value) {
    g_log.line("player settings: player %d: ignoring invalid %s=%s", extra + 3, key, value);
}

bool read_flag(int extra, const char *key, unsigned char &value) {
    char text[32] = "";
    if (!read_text(extra, key, text, sizeof(text))) return false;
    if (!_stricmp(text, "1") || !_stricmp(text, "on") || !_stricmp(text, "true") || !_stricmp(text, "yes")) {
        value = 1;
        return true;
    }
    if (!_stricmp(text, "0") || !_stricmp(text, "off") || !_stricmp(text, "false") || !_stricmp(text, "no")) {
        value = 0;
        return true;
    }
    invalid_value(extra, key, text);
    return false;
}

bool parse_integer(const char *text, int minimum, int maximum, int &value) {
    char *end = nullptr;
    const long long parsed = strtoll(text, &end, 10);
    if (end == text) return false;
    while (*end && isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end || parsed < minimum || parsed > maximum) return false;
    value = static_cast<int>(parsed);
    return true;
}

bool read_camera(int extra, int &camera) {
    char text[32] = "";
    if (!read_text(extra, "camera", text, sizeof(text))) return false;
    for (int i = 0; i < kCameraCount; ++i) {
        if (!_stricmp(text, kCameraNames[i])) {
            camera = i;
            return true;
        }
    }
    if (!_stricmp(text, "super far") || !_stricmp(text, "super-far")) {
        camera = 4;
        return true;
    }
    int number = 0;
    if (parse_integer(text, 0, INT_MAX, number)) {
        camera = number;
        return true;
    }
    invalid_value(extra, "camera", text);
    return false;
}

bool read_transmission(int extra, unsigned char &transmission) {
    char text[32] = "";
    if (!read_text(extra, "transmission", text, sizeof(text))) return false;
    if (!_stricmp(text, "automatic") || !_stricmp(text, "auto") || !_stricmp(text, "0")) {
        transmission = 0;
        return true;
    }
    if (!_stricmp(text, "manual") || !_stricmp(text, "1")) {
        transmission = 1;
        return true;
    }
    invalid_value(extra, "transmission", text);
    return false;
}

bool read_controller_config(int extra, Settings &settings) {
    char text[32] = "";
    if (!read_text(extra, "controller_config", text, sizeof(text))) return false;
    int number = 0;
    if (!parse_integer(text, 1, 10, number)) {
        invalid_value(extra, "controller_config", text);
        return false;
    }
    settings.drive_with_analog = number <= kControllerConfigurations;
    settings.controller_config = (number - 1) % kControllerConfigurations;
    return true;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool read_legacy(int extra, Settings &legacy, bool &present) {
    char text[2 * kSettingsSize + 2] = "";  // the extra character lets an overlong value fail the exact-length check
    read_text(extra, "settings", text, sizeof(text));
    present = text[0] != '\0';
    if (!present || strlen(text) != 2 * kSettingsSize) return false;
    unsigned char bytes[kSettingsSize];
    for (unsigned i = 0; i < kSettingsSize; ++i) {
        const int high = hex_digit(text[2 * i]), low = hex_digit(text[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        bytes[i] = static_cast<unsigned char>((high << 4) | low);
    }
    memcpy(&legacy, bytes, sizeof(legacy));
    return true;
}

void import_legacy(const Settings &legacy, Settings &settings) {
    settings.gauges = legacy.gauges != 0;
    settings.position = legacy.position != 0;
    settings.score = legacy.score != 0;
    settings.vibration = legacy.vibration != 0;
    settings.leaderboard = legacy.leaderboard != 0;
    settings.drive_with_analog = legacy.drive_with_analog != 0;
    if (legacy.controller_config >= 0 && legacy.controller_config < kControllerConfigurations) settings.controller_config = legacy.controller_config;
    if (legacy.camera >= 0) settings.camera = legacy.camera;
    if (legacy.split_time <= 4) settings.split_time = legacy.split_time == 4 ? 4 : 0;
    if (legacy.transmission <= 1) settings.transmission = legacy.transmission;
}

}  // namespace

bool load(int extra, unsigned char *out) {
    if (!out || extra < 0 || extra >= nfsmw::kMaxExtras) return false;
    Settings settings;
    memcpy(&settings, out, sizeof(settings));  // defaults copied from player 1 or 2 by more_players.cpp

    Settings legacy;
    bool legacy_present = false;
    bool found = read_legacy(extra, legacy, legacy_present);
    if (found) {
        import_legacy(legacy, settings);
        g_log.line("player settings: player %d: importing the old settings= value", extra + 3);
    } else if (legacy_present) {
        g_log.line("player settings: player %d: discarding invalid old settings= value", extra + 3);
    }

    found = read_transmission(extra, settings.transmission) || found;
    found = read_camera(extra, settings.camera) || found;
    found = read_flag(extra, "gauges", settings.gauges) || found;
    found = read_flag(extra, "position", settings.position) || found;
    unsigned char split_time = settings.split_time == 4 ? 0 : 1;
    if (read_flag(extra, "split_time", split_time)) {
        settings.split_time = split_time ? 0 : 4;
        found = true;
    }
    found = read_flag(extra, "score", settings.score) || found;
    found = read_flag(extra, "leaderboard", settings.leaderboard) || found;
    found = read_flag(extra, "vibration", settings.vibration) || found;
    found = read_controller_config(extra, settings) || found;

    memcpy(out, &settings, sizeof(settings));
    if (legacy_present) save(extra, out);  // one-time migration to named values, and removal of the raw value
    return found;
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

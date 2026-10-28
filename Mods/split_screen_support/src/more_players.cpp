/*
 * The race with 3 or 4 players runs the game's own two player menu flow twice. A split screen race starts when the second player has chosen
 * a car: UIQRCarSelect::CommitChangeStartRace deletes the second player's temporary profile, which saves their car in the FEDatabase (the
 * car's name at +0x470 and a malloc'd copy of its customization, 0x198 bytes, at +0x46C: DeleteMultiplayerProfile 0x58FFA0), and calls
 * RaceStarter::StartRace (0x57ECA0). The first player's car is the handle in the Quick Race settings (GetQuickRaceSettings 0x56DBE0 with
 * the "all types" settings 11; selected car 0 at +0x10, 1 at +0x14).
 *
 * When more than 2 players are wanted (player_count.cpp), StartRace is not called after the first round: the picks of players 1 and 2 are
 * kept in a table of ours, and the game is sent back to Press Start for the second round (PressStart.fng, player 0), whose screens are
 * the same ones: their choices are players 3 and 4 (with three players the first player's choice of a car ends the round: the switch to the next Press Start
 * is the start of the race instead). After the second round the first round's picks go back where the game reads them,
 * players 3 and 4 are kept in the table, and StartRace is called: the race has players 1 and 2 (the cars of 3 and 4 come with the race
 * code that creates more players).
 * The screens only ever know players 0 and 1, so none of the game's per player arrays (profiles, settings, joystick ports) has to grow.
 *
 * In the race (QuickGame::OnBeginState, state "loading players": CreatePlayers 0x6F2700 at 0x6F4EDA, then CreateCars 0x6F47A0 at 0x6F4EE5, each
 * called from that one place) the same trick makes players 3 and 4. CreateCars builds the first player's car from the Quick Race settings' selected
 * car and the second player's from the name and customization in the FEDatabase (the picks above), binding them to the first and the last entry
 * of the local player list (array at 0x92D87C, count at 0x92D884). So after the original CreateCars (two players in the list, or the second car
 * would go to the last of four), CreatePlayers is called again for two more LocalPlayers (controller and render port taken away), and CreateCars a
 * second time with the list reduced to those two and the picks of the second round where the game reads them. The start position of that call is
 * two steps further along the line between the first two cars. Then the two leave the local player list (they stay in the list of all players,
 * which keeps their cars): a HUD of theirs would be a second copy of player 1's or 2's (same FEng package, on the same view, writing into the same
 * text), and none crashes the loops that ask every local player for it. The cars stand where they were made: nothing drives them yet.
 */
#include "more_players.h"

#include <algorithm>
#include <cmath>

#include "hooks.h"
#include "max_players.h"
#include "fixes.h"
#include "view_ids.h"
#include "patch.h"
#include "player_count.h"
#include "player_settings.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::more_players {

namespace {

constexpr uintptr_t kFeDatabase = 0x91CF90, kFEng = 0x91CADC, kGetQuickRaceSettings = 0x56DBE0, kQueuePackageSwitch = 0x525940, kBMalloc = 0x465A70,
                    kBFree = 0x4655F0, kFePrintfPackage = 0x5252D0, kGetLocalizedString = 0x56BC10, kStartRace = 0x57ECA0,
                    kCommitCallSite = 0x7A3B09;  // the call of StartRace in UIQRCarSelect's commit function
constexpr uintptr_t kSplitMode = 0x988E04;  // 1 in a split screen game: CreatePlayers makes the second player, CreateCars the second car
constexpr uintptr_t kCreatePlayersCall = 0x6F4EDA, kCreatePlayers = 0x6F2700, kCreateCarsCall = 0x6F4EE5, kCreateCars = 0x6F47A0, kPlayerArray = 0x92D87C,
                    kPlayerCount = 0x92D884;
constexpr unsigned kSetRenderPortSlot = 0x28 / 4, kSetControllerPortSlot = 0x30 / 4;  // IPlayer's virtual table
constexpr int kAllTypes = 11;
constexpr float kRowSpacing = 9.0f;  // metres between the two rows of the starting grid
constexpr unsigned kSelectedCar = 0x10, kSplitCarName = 0x470, kSplitCustomization = 0x46C, kCustomizationSize = 0x198, kPressStartPlayer = 0x2C,
                   kScreenPackage = 0x10;
constexpr unsigned kPlayerTextObject = 0xB244CF71, kPlayerTextFormat = 0xCF538E1C;  // "Player %d press start": the object and the language string

struct Pick {
    bool valid = false;
    unsigned car_handle = 0;  // the first player of a round: the stable's car (the second player's is in `car` and `customization`)
    char car[32] = "";
    bool has_customization = false;
    unsigned char customization[kCustomizationSize];
    int device = 0;
};
Pick g_picks[nfsmw::kMaxPlayers];  // the players of all rounds
int g_round = 0;

unsigned char *database() { return *reinterpret_cast<unsigned char **>(kFeDatabase); }
unsigned *selected_car(int player) {
    using Fn = unsigned char *(__fastcall *)(void *, void *, int);
    return reinterpret_cast<unsigned *>(reinterpret_cast<Fn>(kGetQuickRaceSettings)(database(), nullptr, kAllTypes) + kSelectedCar + 4 * player);
}
void **customization_pointer() { return reinterpret_cast<void **>(database() + kSplitCustomization); }
char *car_name() { return reinterpret_cast<char *>(database() + kSplitCarName); }

// ---- the cars the players chose before: `[player_N] car=` in the ini (the handle of the car in the stable of the screen's player: SelectedCar[0] or [1] of the
// Quick Race settings, which the game sets when a player accepts a car and the Car Select screen starts on). The next Car Select of that player starts on it.
constexpr unsigned kNoCar = 0x12345678;  // what SelectedCar holds when nothing was chosen
unsigned g_remembered[nfsmw::kMaxPlayers];
bool g_remembered_read = false;

void read_remembered() {
    if (g_remembered_read) return;
    g_remembered_read = true;
    for (int player = 0; player < nfsmw::kMaxPlayers; ++player) {
        char section[16];
        snprintf(section, sizeof(section), "player_%d", player + 1);
        g_remembered[player] = static_cast<unsigned>(GetPrivateProfileIntA(section, "car", 0, g_ini));
    }
}

void remember(int player, unsigned handle) {
    read_remembered();
    if (player < 0 || player >= nfsmw::kMaxPlayers || !handle || handle == kNoCar || g_remembered[player] == handle) return;
    g_remembered[player] = handle;
    char section[16], value[16];
    snprintf(section, sizeof(section), "player_%d", player + 1);
    snprintf(value, sizeof(value), "%u", handle);
    WritePrivateProfileStringA(section, "car", value, g_ini);
}

// Before the Car Select of a player (`slot` 0 or 1 is the screen's player): the car they chose last starts selected. The screen's setup looks the handle up in the
// player's stable and starts on the first car when it is not there (the stable of the second player is only made by the screen itself, so it cannot be asked yet).
void preselect_car(int player, int slot) {
    read_remembered();
    if (player < 0 || player >= nfsmw::kMaxPlayers || !g_remembered[player]) return;
    *selected_car(slot) = g_remembered[player];
    g_log.line("Car Select: player %d starts on the car they chose before (handle %#x)", player + 1, g_remembered[player]);
}

// What the first player and the second player of the finished round chose goes into g_picks[first] and [first + 1] (`count` 1: the first player only).
void capture(int first, int count = 2) {
    Pick &p1 = g_picks[first], &p2 = g_picks[first + 1];
    int devices[2];
    players::get_chosen(devices);
    p1.valid = true;
    p2.valid = count > 1;
    p1.car_handle = *selected_car(0);
    p1.device = devices[0];
    remember(first, *selected_car(0));
    if (count > 1) remember(first + 1, *selected_car(1));
    // A diagnostic: every player drives this car ([options] test_car_handle: the handle of a car of the profile's garage, test_car: the name of its folder in CARS).
    char test_name[32];
    GetPrivateProfileStringA("options", "test_car", "", test_name, sizeof(test_name), g_ini);
    const int test_handle = GetPrivateProfileIntA("options", "test_car_handle", 0, g_ini);
    if (test_handle > 0 && test_name[0]) {
        p1.car_handle = test_handle;
        *selected_car(0) = test_handle;
        strcpy(car_name(), test_name);
        if (void *old = *customization_pointer()) reinterpret_cast<void (*)(void *)>(kBFree)(old);
        *customization_pointer() = nullptr;
        g_log.line("test_car: players %d to %d drive \"%s\" (handle %d)", first + 1, first + count, test_name, test_handle);
    }
    if (count < 2) {
        g_log.line("player %d chose: car handle %#x on device %d", first + 1, p1.car_handle, p1.device);
        return;
    }
    p2.device = devices[1];
    strncpy(p2.car, car_name(), sizeof(p2.car) - 1);
    p2.car[sizeof(p2.car) - 1] = 0;
    void *custom = *customization_pointer();
    p2.has_customization = custom != nullptr;
    if (custom) memcpy(p2.customization, custom, kCustomizationSize);
    g_log.line("players %d and %d chose: car handle %#x on device %d, car \"%s\" on device %d", first + 1, first + 2, p1.car_handle, p1.device, p2.car, p2.device);
}

// Puts the picks of players 1 and 2 back where the game reads them.
void restore_first_round() {
    const Pick &p1 = g_picks[0], &p2 = g_picks[1];
    *selected_car(0) = p1.car_handle;
    strcpy(car_name(), p2.car);
    if (void *old = *customization_pointer()) reinterpret_cast<void (*)(void *)>(kBFree)(old);  // the second round's copy: it is in the table
    void *copy = nullptr;
    if (p2.has_customization) {
        copy = reinterpret_cast<void *(*)(int, int)>(kBMalloc)(kCustomizationSize, 0x47);
        if (copy) memcpy(copy, p2.customization, kCustomizationSize);
    }
    *customization_pointer() = copy;
    const int devices[2] = {p1.device, p2.device};
    players::set_chosen(devices);
}

uintptr_t g_start_race = kStartRace;  // what the commit function called before the redirect: StartRace, or another mod's function that calls it

// How many players the Players row asked for, and how they are shared among the rounds of Press Start / Car Select (the game's screens know two players, so
// each round has two, the last one one when the count is odd).
int wanted_players() { return std::min(player_count::wanted(), nfsmw::kMaxPlayers); }
int rounds_wanted() { return (wanted_players() + nfsmw::kPlayersPerRound - 1) / nfsmw::kPlayersPerRound; }
int players_in_round(int round) { return std::min(nfsmw::kPlayersPerRound, wanted_players() - round * nfsmw::kPlayersPerRound); }

// What the car select's commit function (0x7A3AF0) calls instead of RaceStarter::StartRace.
void start_race() {
    const auto original_start_race = reinterpret_cast<void (*)()>(g_start_race);
    if (!players::split_screen() || player_count::wanted() < 3) {
        g_round = 0;
        if (players::split_screen()) {
            remember(0, *selected_car(0));
            remember(1, *selected_car(1));
        }
        original_start_race();
        return;
    }
    capture(g_round * nfsmw::kPlayersPerRound, players_in_round(g_round));
    if (g_round + 1 < rounds_wanted()) {
        ++g_round;
        g_log.line("round %d: Press Start and Car Select for players %d to %d", g_round + 1, g_round * nfsmw::kPlayersPerRound + 1,
                   g_round * nfsmw::kPlayersPerRound + players_in_round(g_round));
        reinterpret_cast<void(__fastcall *)(void *, void *, const char *, int, int, bool)>(kQueuePackageSwitch)(
            *reinterpret_cast<void **>(kFEng), nullptr, "PressStart.fng", 0, 0, false);
        return;
    }
    restore_first_round();
    g_round = 0;
    g_log.line("all %d players chose; the race starts with players 1 and 2 (the others are made after their cars)", wanted_players());
    fixes::vehicle_cache_room();
    original_start_race();
}

// ---- the race: players 3 and 4

// How many players besides the first two the race that is about to load (or is running) has (0 until the picks of every round are there).
int extras_wanted() {
    if (!players::split_screen() || player_count::wanted() < 3) return 0;
    const int extras = wanted_players() - nfsmw::kPlayersPerRound;
    for (int i = 0; i < extras; ++i)
        if (!g_picks[nfsmw::kPlayersPerRound + i].valid) return 0;
    return extras;
}

uintptr_t g_create_players = kCreatePlayers, g_create_cars = kCreateCars;  // what the calls go to (another mod may have redirected them)

void **&player_array() { return *reinterpret_cast<void ***>(kPlayerArray); }
int &player_count_value() { return *reinterpret_cast<int *>(kPlayerCount); }

// The car of a player as the game's physics has it, in sim space: position, forward and up vectors. ISimable (the car) virtual table +0x54 is
// GetRigidBody; IRigidBody's +0x20 is GetPosition, +0x34 GetForwardVector, +0x3C GetUpVector (the decomp's order, one slot further for the base
// interface) [tested: unit vectors, the up vector points up].
bool rigid_body_vectors(void *player, float *position, float *forward, float *up) {
    void **player_vtable = *reinterpret_cast<void ***>(player);
    void *simable = reinterpret_cast<void *(__fastcall *)(void *, void *)>(player_vtable[1])(player, nullptr);
    if (!simable) return false;
    void **simable_vtable = *reinterpret_cast<void ***>(simable);
    void *body = reinterpret_cast<void *(__fastcall *)(void *, void *)>(simable_vtable[0x54 / 4])(simable, nullptr);
    if (!body) return false;
    void **vtable = *reinterpret_cast<void ***>(body);
    const float *at = reinterpret_cast<const float *(__fastcall *)(void *, void *)>(vtable[0x20 / 4])(body, nullptr);
    memcpy(position, at, 3 * sizeof(float));
    reinterpret_cast<void(__fastcall *)(void *, void *, float *)>(vtable[0x34 / 4])(body, nullptr, forward);
    reinterpret_cast<void(__fastcall *)(void *, void *, float *)>(vtable[0x3C / 4])(body, nullptr, up);
    return true;
}

const float *player_position(void *player) {
    void **vtable = *reinterpret_cast<void ***>(player);
    return static_cast<const float *>(reinterpret_cast<void *(__fastcall *)(void *, void *)>(vtable[0xC / 4])(player, nullptr));  // IPlayer::GetPosition
}

void set_player_port(void *player, unsigned slot, int value) {
    void **vtable = *reinterpret_cast<void ***>(player);
    reinterpret_cast<void(__fastcall *)(void *, void *, int)>(vtable[slot])(player, nullptr, value);
}

// More LocalPlayers, by calling CreatePlayers again (they get the first two players' ports and settings, which are then taken away: no view, no device yet): two
// of them for each call, or one when the game is not in split screen mode for the call (CreatePlayers makes the second player only in that mode). Only after
// CreateCars made the first two cars: it gives the second player's car to the last player of the list.
bool make_extra_players(void *quick_game, void *edx, int extras) {
    using Fn = void(__fastcall *)(void *, void *);
    int &split_mode = *reinterpret_cast<int *>(kSplitMode);
    const int saved_mode = split_mode;
    for (int made = 0; made < extras; made += nfsmw::kPlayersPerRound) {
        if (extras - made == 1) split_mode = 0;
        reinterpret_cast<Fn>(g_create_players)(quick_game, edx);
        split_mode = saved_mode;
    }
    const int count = player_count_value();
    g_log.line("local players: %d (the calls of CreatePlayers made %d more)", count, extras);
    if (count < nfsmw::kPlayersPerRound + extras) return false;
    for (int i = count - extras; i < count; ++i) {
        void *player = player_array()[i];
        set_player_port(player, kSetRenderPortSlot, -1);  // no view of the game's own: the views of the extra players are made by extra_cameras.cpp
        set_player_port(player, kSetControllerPortSlot, players::extra_port(i - (count - extras)));  // their device (players.cpp), -1 when they have none
    }
    return true;
}

// IPlayer::GetSimable (virtual table slot 1): the car of a player, null until a car is made for them.
void log_player_cars(const char *when) {
    const int count = player_count_value();
    for (int i = 0; i < count; ++i) {
        void *player = player_array()[i];
        void **vtable = *reinterpret_cast<void ***>(player);
        void *simable = reinterpret_cast<void *(__fastcall *)(void *, void *)>(vtable[1])(player, nullptr);
        g_log.line("%s: local player %d (%p) has car %p", when, i + 1, player, simable);
    }
}

void *g_extra_players[nfsmw::kMaxExtras];

// ---- their own settings
// The game keeps a PlayerSettings (0x14 bytes, array at [FEDatabase + 0x10] + 0x80) for players 1 and 2 only, and the other players had those: pressing
// Change Camera View as player 3 changed player 1's camera. Each extra player gets a copy of the settings of player 1 or 2, handed out by a copy of
// their IPlayer's virtual table whose GetSettings (+0x14) is ours.
constexpr unsigned kSettingsArray = 0x80, kSettingsSize = 0x14, kGetSettingsSlot = 0x14 / 4, kVTableSlots = 32;
unsigned char *g_extra_settings[nfsmw::kMaxExtras];
void *(__fastcall *g_orig_get_settings)(void *, void *);

void *__fastcall extra_get_settings(void *self, void *edx) {
    for (int i = 0; i < nfsmw::kMaxExtras; ++i)
        if (self == g_extra_players[i] && g_extra_settings[i]) return g_extra_settings[i];
    return g_orig_get_settings(self, edx);
}

void give_own_settings(int index, void *player) {
    unsigned char *database = *reinterpret_cast<unsigned char **>(kFeDatabase);
    unsigned char *all = *reinterpret_cast<unsigned char **>(database + 0x10) + kSettingsArray;
    if (!g_extra_settings[index]) g_extra_settings[index] = static_cast<unsigned char *>(malloc(kSettingsSize));
    if (!g_extra_settings[index]) return;
    memcpy(g_extra_settings[index], all + (index % nfsmw::kPlayersPerRound) * kSettingsSize, kSettingsSize);  // player 1's or 2's (the slot their LocalPlayer was made with)
    player_settings::load(index, g_extra_settings[index]);                          // unless they chose their own before (this mod's ini)
    void **original = *reinterpret_cast<void ***>(player);
    if (original[kGetSettingsSlot] == reinterpret_cast<void *>(&extra_get_settings)) return;
    void **table = static_cast<void **>(malloc(kVTableSlots * sizeof(void *)));
    if (!table) return;
    memcpy(table, original, kVTableSlots * sizeof(void *));
    g_orig_get_settings = reinterpret_cast<void *(__fastcall *)(void *, void *)>(table[kGetSettingsSlot]);
    table[kGetSettingsSlot] = reinterpret_cast<void *>(&extra_get_settings);
    *reinterpret_cast<void ***>(player) = table;
}

void __fastcall create_cars(void *quick_game, void *edx, const float *start) {
    using Fn = void(__fastcall *)(void *, void *, const float *);
    reinterpret_cast<Fn>(g_create_cars)(quick_game, edx, start);
    const int extras = extras_wanted();
    if (!extras || !make_extra_players(quick_game, edx, extras)) return;
    log_player_cars("after the first CreateCars");

    void **saved_array = player_array();
    const int saved_count = player_count_value();
    const unsigned saved_car = *selected_car(0);
    char saved_name[32];
    strncpy(saved_name, car_name(), sizeof(saved_name) - 1);
    saved_name[sizeof(saved_name) - 1] = 0;

    void *extra_list[nfsmw::kMaxExtras] = {};
    for (int i = 0; i < extras; ++i) {
        extra_list[i] = player_array()[saved_count - extras + i];
        g_extra_players[i] = extra_list[i];
        give_own_settings(i, extra_list[i]);
    }

    // A grid: the game puts the second car beside the first, and the extra cars go in rows behind them (the same step apart) along the way the first car faces.
    const float *first = player_position(saved_array[0]), *second = player_position(saved_array[1]);
    float row[2];  // the step from one row to the next, in the x, z plane of the simulation
    float own_position[4], forward[4], up[4];
    if (rigid_body_vectors(saved_array[0], own_position, forward, up) && hypotf(forward[0], forward[2]) > 0.1f) {
        const float length = hypotf(forward[0], forward[2]);
        row[0] = -forward[0] / length * kRowSpacing;
        row[1] = -forward[2] / length * kRowSpacing;
    } else {
        row[0] = 2 * (second[0] - first[0]);  // no direction: further along the line between the first two
        row[1] = 2 * (second[2] - first[2]);
    }
    g_log.line("first two cars at (%.1f, %.1f) and (%.1f, %.1f): the rows of the other cars are (%.1f, %.1f) apart", first[0], first[2], second[0], second[2], row[0], row[1]);
    int &split_mode = *reinterpret_cast<int *>(kSplitMode);
    const int saved_mode = split_mode;
    for (int made = 0, group = 0; made < extras; made += nfsmw::kPlayersPerRound, ++group) {
        const int in_group = std::min(nfsmw::kPlayersPerRound, extras - made);
        void *list[nfsmw::kPlayersPerRound] = {};
        for (int i = 0; i < in_group; ++i) list[i] = extra_list[made + i];
        player_array() = reinterpret_cast<void **>(list);
        player_count_value() = in_group;
        const Pick &first_pick = g_picks[nfsmw::kPlayersPerRound + made];
        *selected_car(0) = first_pick.car_handle;
        void *custom = nullptr;
        if (in_group == 2) {  // the second player's car is the one named in the FEDatabase
            const Pick &second_pick = g_picks[nfsmw::kPlayersPerRound + made + 1];
            strcpy(car_name(), second_pick.car);
            if (second_pick.has_customization) {
                custom = reinterpret_cast<void *(*)(int, int)>(kBMalloc)(kCustomizationSize, 0x47);
                if (custom) memcpy(custom, second_pick.customization, kCustomizationSize);
            }
        }
        *customization_pointer() = custom;  // the game frees it after use
        float shifted[4] = {start[0] + row[0] * (group + 1), start[1], start[2] + row[1] * (group + 1), start[3]};
        g_log.line("cars of players %d to %d: start (%.1f, %.1f, %.1f), car handle %#x", nfsmw::kPlayersPerRound + made + 1, nfsmw::kPlayersPerRound + made + in_group,
                   shifted[0], shifted[1], shifted[2], first_pick.car_handle);
        if (in_group == 1) split_mode = 0;  // one car only: the second is made in split screen mode
        reinterpret_cast<Fn>(g_create_cars)(quick_game, edx, shifted);
        split_mode = saved_mode;
    }

    player_array() = saved_array;
    // The extra players leave the list of local players (they stay in the list of all players, which keeps their cars): everything the game does
    // for the local players (HUD, camera, view, the pause menu) then does not know them. A HUD of theirs would be a second copy of the HUD of
    // player 1 or 2 (same package) on the same view, and a null one crashes the loops that ask each local player for it.
    player_count_value() = saved_count - extras;
    *selected_car(0) = saved_car;
    strcpy(car_name(), saved_name);
    *customization_pointer() = nullptr;
    g_log.line("cars of the extra players made");
    log_player_cars("after the second CreateCars");
}

}  // namespace

// What the game made for the views: active, render target, camera, and whether the view has camera movers (the list head points at itself when empty).
void log_views() {
    for (const int id : {1, 2, 4, 5}) {
        const unsigned char *view = reinterpret_cast<const unsigned char *>(0x9195E0 + id * 0x70);
        const void *const *head = reinterpret_cast<const void *const *>(view + 0x44);
        g_log.line("view %d: id %d, active %d, render target %p, camera %p, camera movers %s, pixel minimum size %d", id, *reinterpret_cast<const int *>(view + 4),
                   view[8], *reinterpret_cast<void *const *>(view + 0x60), *reinterpret_cast<void *const *>(view + 0x40), head[0] == head ? "none" : "yes",
                   *reinterpret_cast<const int *>(view + 0x24));
    }
}

void log_players() {
    log_views();
    for (int i = 0; i < nfsmw::kMaxPlayers; ++i) {
        void *player = game_player(i);
        if (!player) continue;
        void **vtable = *reinterpret_cast<void ***>(player);
        const auto call = [&](unsigned slot) { return reinterpret_cast<void *(__fastcall *)(void *, void *)>(vtable[slot])(player, nullptr); };
        const float *position = static_cast<const float *>(call(0xC / 4));  // GetPosition
        const unsigned char *settings = reinterpret_cast<const unsigned char *(__fastcall *)(void *, void *)>(vtable[0x14 / 4])(player, nullptr);
        g_log.line("player %d: camera view %d (its settings %p)", i + 1, settings ? *reinterpret_cast<const int *>(settings + 0xC) : -1, settings);
        g_log.line("player %d: car %p, position (%.1f, %.1f, %.1f), render port %d", i + 1, call(1), position[0], position[1], position[2],
                   static_cast<int>(reinterpret_cast<intptr_t>(call(0x2C / 4))));
        float v[3][4] = {};
        if (rigid_body_vectors(player, v[0], v[1], v[2]))
            g_log.line("    rigid body: position (%.1f, %.1f, %.1f), forward (%.2f, %.2f, %.2f), up (%.2f, %.2f, %.2f)", v[0][0], v[0][1], v[0][2], v[1][0], v[1][1],
                       v[1][2], v[2][0], v[2][1], v[2][2]);
    }
}

int extra_player_count() {
    int count = 0;
    for (void *player : g_extra_players) count += player ? 1 : 0;
    return count;
}

int pick_device(int player) { return player >= 0 && player < nfsmw::kMaxPlayers && g_picks[player].valid ? g_picks[player].device : -2; }

bool device_used_in_earlier_round(int device) {
    for (int i = 0; i < g_round * nfsmw::kPlayersPerRound; ++i)
        if (g_picks[i].valid && g_picks[i].device == device) return true;
    return false;
}

// The LocalPlayer of a player (0-based), null when there is none (players 1 and 2: the local player list, the others: the extra players).
void *game_player(int index) {
    if (index < 0 || index >= nfsmw::kMaxPlayers) return nullptr;
    if (index >= nfsmw::kPlayersPerRound) return g_extra_players[index - nfsmw::kPlayersPerRound];
    return index < player_count_value() ? player_array()[index] : nullptr;
}

void *extra_player(int index) { return index >= 0 && index < nfsmw::kMaxExtras ? g_extra_players[index] : nullptr; }

unsigned char *extra_settings(int index) { return index >= 0 && index < nfsmw::kMaxExtras ? g_extra_settings[index] : nullptr; }

void lend_view(void *player, bool on) {
    for (int i = 0; i < nfsmw::kMaxExtras; ++i)
        if (player && player == g_extra_players[i]) set_player_port(player, kSetRenderPortSlot, on ? nfsmw::views::kPlayerView[nfsmw::kPlayersPerRound + i] : -1);
}

namespace {
void **g_saved_list;
int g_saved_count;
}  // namespace

bool extra_ready(int index) {
    float position[4], forward[4], up[4];
    return index >= 0 && index < nfsmw::kMaxExtras && g_extra_players[index] && player_count_value() >= 2 && rigid_body_vectors(g_extra_players[index], position, forward, up);
}

void make_only_local_player(int index, bool on) {
    static void *list[1];
    if (on) {
        g_saved_list = player_array();
        g_saved_count = player_count_value();
        list[0] = g_extra_players[index];
        player_array() = list;
        player_count_value() = 1;
        lend_view(g_extra_players[index], true);
    } else {
        player_array() = g_saved_list;
        player_count_value() = g_saved_count;
        lend_view(g_extra_players[index], false);
    }
}

int extras_in_race() { return extras_wanted(); }

int race_players() { return !players::split_screen() ? 1 : nfsmw::kPlayersPerRound + extras_wanted(); }

bool player_car_state(int index, float *position, float *forward) {
    void *player = game_player(index);
    float up[4];
    return player && rigid_body_vectors(player, position, forward, up);
}

void reset() {
    g_round = 0;
    for (void *&player : g_extra_players) player = nullptr;
    for (Pick &pick : g_picks) pick.valid = false;
}

int round() { return g_round; }

// The Car Select screen of a split screen race says which player it is for and with which device: the game's label (the language string 0x7B070984 /
// 0x7B070985, "Player 1" / "Player 2" in the object 0xE3FE27FE) with the number changed (3 and 4 in the second round), and the device's name as the
// Controls menu shows it, for the player whose Press Start came just before.
int g_press_start_player = 0;  // the player of the last Press Start screen of this round: 0 or 1
constexpr unsigned kCarSelectPlayerObject = 0xE3FE27FE, kPlayerOneString = 0x7B070984;

// "Player 3 - Xbox One S Controller": the game's "Player 1" text with the number changed, and the device's name as the Controls menu shows it.
void format_player_name(int number, int device, char *text, unsigned size) {
    const char *label = reinterpret_cast<const char *(*)(unsigned)>(kGetLocalizedString)(kPlayerOneString);
    strncpy(text, label ? label : "PLAYER 1", size - 1);
    text[size - 1] = 0;
    for (char *c = text + strlen(text); c-- > text;)
        if (*c >= '0' && *c <= '9') {
            *c = static_cast<char>('0' + number);
            break;
        }
    char name[96];
    players::device_label(device, name, sizeof(name));
    if (name[0]) snprintf(text + strlen(text), size - strlen(text), " - %s", name);
}

void *(*g_orig_create_car_select)(void *);
void *hook_create_car_select(void *data) {
    if (players::split_screen()) preselect_car(2 * g_round + g_press_start_player, g_press_start_player);
    void *screen = g_orig_create_car_select(data);
    if (!screen || !players::split_screen()) return screen;
    char text[160];
    format_player_name(2 * g_round + g_press_start_player + 1, players::chosen(g_press_start_player), text, sizeof(text));
    const char *package = *reinterpret_cast<const char **>(static_cast<char *>(screen) + kScreenPackage);
    // The label is a group of strings (the text and its shadow). A string has room for 15 characters (FEPrintf cuts the text there), so they are assigned
    // with the game's own FEWideString::operator= (0x57E8C0), which makes room, and the flags FEPrintf sets: do not take the text from the language string
    // (2), the text is new (0x400000).
    if (void *group = reinterpret_cast<void *(*)(const char *, unsigned)>(0x524850)(package, kCarSelectPlayerObject)) {  // FEngFindObject
        for (void *child = *reinterpret_cast<void **>(static_cast<char *>(group) + 0x64); child; child = *reinterpret_cast<void **>(static_cast<char *>(child) + 4)) {
            char *o = static_cast<char *>(child);
            if (*reinterpret_cast<int *>(o + 0x18) != 2) continue;  // a string
            reinterpret_cast<void *(__fastcall *)(void *, void *, const char *)>(0x57E8C0)(o + 0x64, nullptr, text);
            *reinterpret_cast<unsigned *>(o + 0x1C) |= (1u << 25) | (1u << 24) | 0x400002u;
        }
    }
    g_log.line("Car Select: \"%s\"", text);
    return screen;
}

void player_name(int player, char *text, unsigned size) {
    format_player_name(player + 1, player < 2 ? players::chosen(player) : pick_device(player), text, size);
}

void on_press_start_screen(void *screen) {
    g_press_start_player = *reinterpret_cast<int *>(static_cast<char *>(screen) + kPressStartPlayer) & 1;
    if (g_round < 1) return;
    const int player = g_round * nfsmw::kPlayersPerRound + g_press_start_player + 1;
    const char *package = *reinterpret_cast<const char **>(static_cast<char *>(screen) + kScreenPackage);
    const char *format = reinterpret_cast<const char *(*)(unsigned)>(kGetLocalizedString)(kPlayerTextFormat);
    reinterpret_cast<int (*)(const char *, int, const char *, ...)>(kFePrintfPackage)(package, kPlayerTextObject, format, player);
}

void install_car_select_label() {
    const nfsmw::Hook hooks[] = {
        {"carselectlabel", "CreateQRCarSelect", 0x570C00, {0x6A, 0xFF, 0x68, 0x94, 0x17, 0x87, 0x00}, false, nfsmw::detour(&hook_create_car_select),
         nfsmw::orig(&g_orig_create_car_select)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "car select label");
}

// UIQRCarSelect::NotificationMessage (0x7C1770) sends the first player's choice on to the second player's Press Start screen (cFEng::QueuePackageSwitch, one
// call at 0x7C2694 that all the ways of choosing a car jump to, with the screen argument 1: Press Start of the second player), and PAD_BACK of the first player
// to their own Press Start (argument 0; the second player's Back sets their device free and goes to the second Press Start, argument 1 too) [read]. With
// three players the first player of the second round is the last one: the race starts instead of the second Press Start, but not when they went back.
constexpr uintptr_t kPressStartSwitchCall = 0x7C2694, kQueuePackageSwitchFn = 0x525940;
uintptr_t g_queue_package_switch = kQueuePackageSwitchFn;

void __fastcall queue_package_switch(void *feng, void *edx, const char *package, int a, unsigned b, bool c) {
    if (g_round >= 1 && players::split_screen() && players_in_round(g_round) == 1 && package && strcmp(package, "PressStart.fng") == 0 && a == 1) {
        g_log.line("the last round has one player: their car is chosen; the race starts");
        start_race();
        return;
    }
    reinterpret_cast<void(__fastcall *)(void *, void *, const char *, int, unsigned, bool)>(g_queue_package_switch)(feng, edx, package, a, b, c);
}

void install() {
    if (const uintptr_t previous = nfsmw::chain_call(kPressStartSwitchCall, reinterpret_cast<uintptr_t>(&queue_package_switch))) g_queue_package_switch = previous;
    else g_log.line("more players: the call at %#x is not a call; 3 players will ask for a second Press Start", static_cast<unsigned>(kPressStartSwitchCall));
    // The call is redirected, not StartRace hooked: other mods change functions too (see player_count.cpp).
    if (const uintptr_t target = nfsmw::call_target(kCreatePlayersCall)) g_create_players = target;
    if (const uintptr_t previous = nfsmw::chain_call(kCreateCarsCall, reinterpret_cast<uintptr_t>(&create_cars))) g_create_cars = previous;
    if (const uintptr_t previous = nfsmw::chain_call(kCommitCallSite, reinterpret_cast<uintptr_t>(&start_race))) {
        g_start_race = previous;
        g_log.line("more players: the race start of the car select will be held for the second round (it called %#x%s)", static_cast<unsigned>(previous),
                   previous == kStartRace ? ", StartRace" : ", not StartRace: another mod redirected it");
    } else {
        g_log.line("more players: the call at %#x is not a call; no more than 2 players", static_cast<unsigned>(kCommitCallSite));
    }
}

}  // namespace split_screen_support::more_players

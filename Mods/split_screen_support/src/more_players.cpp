/*
 * The race with 4 players runs the game's own two player menu flow twice. A split screen race starts when the second player has chosen
 * a car: UIQRCarSelect::CommitChangeStartRace deletes the second player's temporary profile, which saves their car in the FEDatabase (the
 * car's name at +0x470 and a malloc'd copy of its customization, 0x198 bytes, at +0x46C: DeleteMultiplayerProfile 0x58FFA0), and calls
 * RaceStarter::StartRace (0x57ECA0). The first player's car is the handle in the Quick Race settings (GetQuickRaceSettings 0x56DBE0 with
 * the "all types" settings 11; selected car 0 at +0x10, 1 at +0x14).
 *
 * When more than 2 players are wanted (player_count.cpp), StartRace is not called after the first round: the picks of players 1 and 2 are
 * kept in a table of ours, and the game is sent back to Press Start for the second round (PressStart.fng, player 0), whose screens are
 * the same ones: their choices are players 3 and 4. After the second round the first round's picks go back where the game reads them,
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

#include <cmath>

#include "hooks.h"
#include "patch.h"
#include "player_count.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::more_players {

namespace {

constexpr uintptr_t kFeDatabase = 0x91CF90, kFEng = 0x91CADC, kGetQuickRaceSettings = 0x56DBE0, kQueuePackageSwitch = 0x525940, kBMalloc = 0x465A70,
                    kBFree = 0x4655F0, kFePrintfPackage = 0x5252D0, kGetLocalizedString = 0x56BC10, kStartRace = 0x57ECA0,
                    kCommitCallSite = 0x7A3B09;  // the call of StartRace in UIQRCarSelect's commit function
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
Pick g_picks[4];  // the players of both rounds
int g_round = 0;

unsigned char *database() { return *reinterpret_cast<unsigned char **>(kFeDatabase); }
unsigned *selected_car(int player) {
    using Fn = unsigned char *(__fastcall *)(void *, void *, int);
    return reinterpret_cast<unsigned *>(reinterpret_cast<Fn>(kGetQuickRaceSettings)(database(), nullptr, kAllTypes) + kSelectedCar + 4 * player);
}
void **customization_pointer() { return reinterpret_cast<void **>(database() + kSplitCustomization); }
char *car_name() { return reinterpret_cast<char *>(database() + kSplitCarName); }

// What the first player and the second player of the finished round chose goes into g_picks[first] and [first + 1].
void capture(int first) {
    Pick &p1 = g_picks[first], &p2 = g_picks[first + 1];
    int devices[2];
    players::get_chosen(devices);
    p1.valid = p2.valid = true;
    p1.car_handle = *selected_car(0);
    p1.device = devices[0];
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

// What the car select's commit function (0x7A3AF0) calls instead of RaceStarter::StartRace.
void start_race() {
    const auto original_start_race = reinterpret_cast<void (*)()>(g_start_race);
    if (!players::split_screen() || player_count::wanted() < 3) {
        g_round = 0;
        original_start_race();
        return;
    }
    if (g_round == 0) {
        capture(0);
        g_round = 1;
        g_log.line("second round: Press Start and Car Select for players 3 and 4");
        reinterpret_cast<void(__fastcall *)(void *, void *, const char *, int, int, bool)>(kQueuePackageSwitch)(
            *reinterpret_cast<void **>(kFEng), nullptr, "PressStart.fng", 0, 0, false);
        return;
    }
    capture(2);
    restore_first_round();
    g_round = 0;
    g_log.line("all 4 players chose; the race starts with players 1 and 2 (3 and 4 are not made yet)");
    original_start_race();
}

// ---- the race: players 3 and 4

bool wants_extra_players() { return players::split_screen() && player_count::wanted() >= 4 && g_picks[2].valid && g_picks[3].valid; }

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

// Two more LocalPlayers, by calling CreatePlayers again (they get the first two players' ports and settings, which are then taken away: no view,
// no device yet). Only after CreateCars made the first two cars: it gives the second player's car to the last player of the list.
bool make_extra_players(void *quick_game, void *edx) {
    using Fn = void(__fastcall *)(void *, void *);
    reinterpret_cast<Fn>(g_create_players)(quick_game, edx);
    const int count = player_count_value();
    g_log.line("local players: %d (the second call of CreatePlayers made players 3 and 4)", count);
    if (count < 4) return false;
    for (int i = count - 2; i < count; ++i) {
        void *player = player_array()[i];
        set_player_port(player, kSetRenderPortSlot, -1);  // no view of the game's own: the views of players 3 and 4 are made by split_view.cpp
        set_player_port(player, kSetControllerPortSlot, players::extra_port(i - (count - 2)));  // their device (players.cpp), -1 when they have none
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

void *g_extra_players[2];

// ---- their own settings
// The game keeps a PlayerSettings (0x14 bytes, array at [FEDatabase + 0x10] + 0x80) for players 1 and 2 only, and players 3 and 4 had those: pressing
// Change Camera View as player 3 changed player 1's camera. Each extra player gets a copy of the settings of player 1 or 2, handed out by a copy of
// their IPlayer's virtual table whose GetSettings (+0x14) is ours.
constexpr unsigned kSettingsArray = 0x80, kSettingsSize = 0x14, kSettingsCurCam = 0xC, kGetSettingsSlot = 0x14 / 4, kVTableSlots = 32;
unsigned char *g_extra_settings[2];
void *(__fastcall *g_orig_get_settings)(void *, void *);

void *__fastcall extra_get_settings(void *self, void *edx) {
    for (int i = 0; i < 2; ++i)
        if (self == g_extra_players[i] && g_extra_settings[i]) return g_extra_settings[i];
    return g_orig_get_settings(self, edx);
}

void give_own_settings(int index, void *player) {
    unsigned char *database = *reinterpret_cast<unsigned char **>(kFeDatabase);
    unsigned char *all = *reinterpret_cast<unsigned char **>(database + 0x10) + kSettingsArray;
    if (!g_extra_settings[index]) g_extra_settings[index] = static_cast<unsigned char *>(malloc(kSettingsSize));
    if (!g_extra_settings[index]) return;
    memcpy(g_extra_settings[index], all + index * kSettingsSize, kSettingsSize);  // player 1's or 2's
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
    if (!wants_extra_players() || !make_extra_players(quick_game, edx)) return;
    log_player_cars("after the first CreateCars");

    void **saved_array = player_array();
    const int saved_count = player_count_value();
    const unsigned saved_car = *selected_car(0);
    char saved_name[32];
    strncpy(saved_name, car_name(), sizeof(saved_name) - 1);
    saved_name[sizeof(saved_name) - 1] = 0;

    void *list[2] = {player_array()[saved_count - 2], player_array()[saved_count - 1]};
    g_extra_players[0] = list[0];
    g_extra_players[1] = list[1];
    for (int i = 0; i < 2; ++i) give_own_settings(i, list[i]);
    player_array() = reinterpret_cast<void **>(list);
    player_count_value() = 2;
    *selected_car(0) = g_picks[2].car_handle;
    strcpy(car_name(), g_picks[3].car);
    void *custom = nullptr;
    if (g_picks[3].has_customization) {
        custom = reinterpret_cast<void *(*)(int, int)>(kBMalloc)(kCustomizationSize, 0x47);
        if (custom) memcpy(custom, g_picks[3].customization, kCustomizationSize);
    }
    *customization_pointer() = custom;  // the game frees it after use

    // A grid: the game puts the second car beside the first, and the extra cars go a row behind them (the same step apart) along the way the first
    // car faces.
    const float *first = player_position(saved_array[0]), *second = player_position(saved_array[1]);
    float shifted[4] = {start[0], start[1], start[2], start[3]};
    float own_position[4], forward[4], up[4];
    if (rigid_body_vectors(saved_array[0], own_position, forward, up) && hypotf(forward[0], forward[2]) > 0.1f) {
        const float length = hypotf(forward[0], forward[2]);
        shifted[0] -= forward[0] / length * kRowSpacing;
        shifted[2] -= forward[2] / length * kRowSpacing;
    } else {
        shifted[0] += 2 * (second[0] - first[0]);  // no direction: further along the line between the first two
        shifted[2] += 2 * (second[2] - first[2]);
    }
    g_log.line("first two cars at (%.1f, %.1f) and (%.1f, %.1f): the cars of players 3 and 4 start at (%.1f, %.1f)", first[0], first[2], second[0], second[2],
               shifted[0], shifted[2]);
    g_log.line("cars of players 3 and 4: start (%.1f, %.1f, %.1f), car handle %#x and \"%s\"", start[0], start[1], start[2], g_picks[2].car_handle,
               g_picks[3].car);
    reinterpret_cast<Fn>(g_create_cars)(quick_game, edx, shifted);

    player_array() = saved_array;
    // Players 3 and 4 leave the list of local players (they stay in the list of all players, which keeps their cars): everything the game does
    // for the local players (HUD, camera, view, the pause menu) then does not know them. A HUD of theirs would be a second copy of the HUD of
    // player 1 or 2 (same package) on the same view, and a null one crashes the loops that ask each local player for it.
    player_count_value() = saved_count - 2;
    *selected_car(0) = saved_car;
    strcpy(car_name(), saved_name);
    *customization_pointer() = nullptr;
    g_log.line("cars of players 3 and 4 made");
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
    void *all[4] = {player_array()[0], player_array()[1], g_extra_players[0], g_extra_players[1]};
    for (int i = 0; i < 4; ++i) {
        void *player = all[i];
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

int extra_player_count() { return g_extra_players[0] && g_extra_players[1] ? 2 : 0; }

int pick_device(int player) { return player >= 0 && player < 4 && g_picks[player].valid ? g_picks[player].device : -2; }

bool device_used_in_first_round(int device) { return g_picks[0].valid && (g_picks[0].device == device || g_picks[1].device == device); }

void *extra_player(int index) { return index >= 0 && index < 2 ? g_extra_players[index] : nullptr; }

void lend_view(void *player, bool on) {
    for (int i = 0; i < 2; ++i)
        if (player && player == g_extra_players[i]) set_player_port(player, kSetRenderPortSlot, on ? i + 1 : -1);  // the view of player 1 or 2
}

bool four_player_race() { return wants_extra_players(); }

bool player_car_state(int index, float *position, float *forward) {
    void *player = index < 2 ? (player_count_value() > index ? player_array()[index] : nullptr) : (index < 4 ? g_extra_players[index - 2] : nullptr);
    float up[4];
    return player && rigid_body_vectors(player, position, forward, up);
}

// The camera of an extra player (0 or 1 for players 3 and 4), in the game's render space (x, y, z of the simulation become z, -x, y: eSwizzleWorldVector),
// for the view type the player chose (their settings' CurCam, which the Change Camera View button scrolls): eye, target and up (4 floats each) and the view
// type (`mode`: 0 bumper, 1 hood, 2 close, 3 far, 4 super far, 5 drift, 6 pursuit). The numbers are rough copies of the game's views: the outside views are
// behind the car and a little above, looking at it, the direction following the car's with a delay (about a tenth of a second); the bumper and hood
// views are in the car and look the way it faces.
struct Preset {
    float behind, height, look_height, ahead;  // metres; `ahead` > 0: an inside view, looking that far ahead
};
constexpr Preset kPresets[7] = {{-2.1f, 0.55f, 0.55f, 20.0f}, {-0.5f, 1.25f, 1.25f, 20.0f}, {5.0f, 1.6f, 1.0f, 0.0f}, {7.5f, 2.4f, 1.2f, 0.0f},
                                {11.0f, 4.0f, 1.2f, 0.0f},    {5.5f, 1.7f, 1.0f, 0.0f},   {6.0f, 2.0f, 1.2f, 0.0f}};

bool extra_camera(int index, float *eye, float *target, float *up, int *mode) {
    if (index < 0 || index > 1 || !g_extra_players[index]) return false;
    float position[4], forward[4], car_up[4];
    if (!rigid_body_vectors(g_extra_players[index], position, forward, car_up)) return false;
    int cam = g_extra_settings[index] ? *reinterpret_cast<int *>(g_extra_settings[index] + kSettingsCurCam) : 6;
    if (cam < 0 || cam > 6) cam = 6;
    *mode = cam;
    static int logged_cam[2] = {-1, -1};
    if (logged_cam[index] != cam) {
        logged_cam[index] = cam;
        g_log.line("player %d: camera view %d (0 bumper, 1 hood, 2 close, 3 far, 4 super far, 5 drift, 6 pursuit)", index + 3, cam);
    }
    const Preset &preset = kPresets[cam];
    static float smooth[2][3];
    static DWORD last_ms[2];
    const DWORD now = GetTickCount();
    const float seconds = last_ms[index] ? (now - last_ms[index]) / 1000.0f : 1.0f;
    last_ms[index] = now;
    float *facing = smooth[index];
    const float length = sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (seconds > 0.5f || (facing[0] * forward[0] + facing[1] * forward[1] + facing[2] * forward[2]) < 0.0f || length < 0.1f) {
        for (int i = 0; i < 3; ++i) facing[i] = forward[i];
    } else {
        const float blend = 1.0f - expf(-seconds / 0.1f);
        for (int i = 0; i < 3; ++i) facing[i] += (forward[i] - facing[i]) * blend;
        const float n = sqrtf(facing[0] * facing[0] + facing[1] * facing[1] + facing[2] * facing[2]);
        for (int i = 0; i < 3; ++i) facing[i] /= n > 0.001f ? n : 1.0f;
    }
    const float *direction = preset.ahead > 0.0f ? forward : facing;
    const float world_up[3] = {0.0f, 1.0f, 0.0f};  // simulation space: y is up
    const auto swizzle = [](const float *v, float *out, float w) {
        out[0] = v[2];
        out[1] = -v[0];
        out[2] = v[1];
        out[3] = w;
    };
    float eye_sim[3], target_sim[3];
    for (int i = 0; i < 3; ++i) {
        eye_sim[i] = position[i] - direction[i] * preset.behind + world_up[i] * preset.height;
        target_sim[i] = preset.ahead > 0.0f ? eye_sim[i] + forward[i] * preset.ahead : position[i] + world_up[i] * preset.look_height;
    }
    swizzle(eye_sim, eye, 1.0f);
    swizzle(target_sim, target, 1.0f);
    swizzle(world_up, up, 0.0f);
    return true;
}

void reset() {
    g_round = 0;
    g_extra_players[0] = g_extra_players[1] = nullptr;
    for (Pick &pick : g_picks) pick.valid = false;
}

int round() { return g_round; }

// The Car Select screen of the second round says "Player 3" or "Player 4": the game's label (the language string 0x7B070984 / 0x7B070985, "Player 1" /
// "Player 2" in the object 0xE3FE27FE) with the number changed, for the player whose Press Start came just before.
int g_label_player = 3;
constexpr unsigned kCarSelectPlayerObject = 0xE3FE27FE, kPlayerOneString = 0x7B070984;

void *(*g_orig_create_car_select)(void *);
void *hook_create_car_select(void *data) {
    void *screen = g_orig_create_car_select(data);
    if (!screen || g_round != 1) return screen;
    char text[64];
    const char *label = reinterpret_cast<const char *(*)(unsigned)>(kGetLocalizedString)(kPlayerOneString);
    strncpy(text, label ? label : "PLAYER 1", sizeof(text) - 1);
    text[sizeof(text) - 1] = 0;
    for (char *c = text + strlen(text); c-- > text;)
        if (*c >= '0' && *c <= '9') {
            *c = static_cast<char>('0' + g_label_player);
            break;
        }
    const char *package = *reinterpret_cast<const char **>(static_cast<char *>(screen) + kScreenPackage);
    reinterpret_cast<int (*)(const char *, int, const char *, ...)>(kFePrintfPackage)(package, kCarSelectPlayerObject, "%s", text);
    return screen;
}

void on_press_start_screen(void *screen) {
    if (g_round != 1) return;
    const int player = *reinterpret_cast<int *>(static_cast<char *>(screen) + kPressStartPlayer) + 3;
    g_label_player = player;
    const char *package = *reinterpret_cast<const char **>(static_cast<char *>(screen) + kScreenPackage);
    const char *format = reinterpret_cast<const char *(*)(unsigned)>(kGetLocalizedString)(kPlayerTextFormat);
    reinterpret_cast<int (*)(const char *, int, const char *, ...)>(kFePrintfPackage)(package, kPlayerTextObject, format, player);
}

void install() {
    const nfsmw::Hook hooks[] = {
        {"carselectlabel", "CreateQRCarSelect", 0x570C00, {0x6A, 0xFF, 0x68, 0x94, 0x17, 0x87, 0x00}, false, nfsmw::detour(&hook_create_car_select),
         nfsmw::orig(&g_orig_create_car_select)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "more players");
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

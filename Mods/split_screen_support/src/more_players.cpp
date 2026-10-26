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
 */
#include "more_players.h"

#include "patch.h"
#include "player_count.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::more_players {

namespace {

constexpr uintptr_t kFeDatabase = 0x91CF90, kFEng = 0x91CADC, kGetQuickRaceSettings = 0x56DBE0, kQueuePackageSwitch = 0x525940, kBMalloc = 0x465A70,
                    kBFree = 0x4655F0, kFePrintfPackage = 0x5252D0, kGetLocalizedString = 0x56BC10, kStartRace = 0x57ECA0,
                    kCommitCallSite = 0x7A3B09;  // the call of StartRace in UIQRCarSelect's commit function
constexpr int kAllTypes = 11;
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

}  // namespace

void reset() { g_round = 0; }

int round() { return g_round; }

void on_press_start_screen(void *screen) {
    if (g_round != 1) return;
    const int player = *reinterpret_cast<int *>(static_cast<char *>(screen) + kPressStartPlayer) + 3;
    const char *package = *reinterpret_cast<const char **>(static_cast<char *>(screen) + kScreenPackage);
    const char *format = reinterpret_cast<const char *(*)(unsigned)>(kGetLocalizedString)(kPlayerTextFormat);
    reinterpret_cast<int (*)(const char *, int, const char *, ...)>(kFePrintfPackage)(package, kPlayerTextObject, format, player);
}

void install() {
    // The call is redirected, not StartRace hooked: other mods change functions too (see player_count.cpp).
    if (const uintptr_t previous = nfsmw::chain_call(kCommitCallSite, reinterpret_cast<uintptr_t>(&start_race))) {
        g_start_race = previous;
        g_log.line("more players: the race start of the car select will be held for the second round (it called %#x%s)", static_cast<unsigned>(previous),
                   previous == kStartRace ? ", StartRace" : ", not StartRace: another mod redirected it");
    } else {
        g_log.line("more players: the call at %#x is not a call; no more than 2 players", static_cast<unsigned>(kCommitCallSite));
    }
}

}  // namespace split_screen_support::more_players

/*
 * The end of a split screen tollbooth race. The game made the race for one player, and it shows:
 *   - "TIME'S UP" to a racer who finished: `RaceOverMessage::RequestRaceOverMessage` (0x58BD10) compares the racer's race time with the race's time limit (`GetTimeLimit`,
 *     the call at 0x58BD6C), and with the booths' bonuses a finishing time is always past it. In a tollbooth race the call answers 0, so the message is the one of any race:
 *     the place and the time ("1ST", "KNOCKED OUT" for a racer whose time ran out);
 *   - the results screen (`PostRaceResultsScreen`) in the mode of the booths' times of one racer; its list of the racers is a mode the tollbooth never reaches
 *     (`PostRaceResultsScreen::SetupResults` has no headings for the race type, and the Y button (PAD_BUTTON4) only switches between the booths and the statistics).
 *     The screen opens on the list, the button goes round the list, the booths' times (L1 and R1 choose the racer) and the statistics, and the list is the circuit's
 *     (`SetupResults` is run as a circuit's), with the time each racer had left at the finish (`RaceResultStat::Draw` 0x5953F0 prints the race time);
 *   - the places come from `GRaceStatus::CalculateRankings` (0x5FDE80), which sorts the racers with a comparison (0x5E31D0, `A after B`): finished racers by race time.
 *     Here the racers who finished are put in the order of the time they had left, the most first (a tie by race time as before).
 * The booths' times page of the screen asks for the finish line time (`SetupLapStats` 0x5972E0), which is the time left of that racer.
 */
#include "tollbooth_results.h"

#include "hooks.h"
#include "patch.h"
#include "racers.h"
#include "split_screen_support.h"
#include "tollbooth.h"

namespace split_screen_support::tollbooth_results {

namespace {

constexpr uintptr_t kGetTimeLimit = 0x5FC260, kMessageLimitCall = 0x58BD6C, kPostRaceSetup = 0x5A19C0, kSetupCall = 0x5A709D, kSetupResults = 0x5964A0, kNotificationMessage = 0x5A7120,
                    kSetupLapStats = 0x5972E0, kResultStatDraw = 0x5953F0, kRankCompare = 0x5E31D0, kPrintToString = 0x6547A0, kFEPrintf = 0x515D70;
constexpr unsigned kScreenRaceType = 0x1E0, kScreenMode = 0x1E4;  // PostRaceResultsScreen (the PC layout)
constexpr unsigned kStatData = 0x30, kStatRacer = 0x50;           // RaceResultStat
constexpr int kRaceCircuit = 1, kRaceTollbooth = 4;
constexpr int kModeResults = 0, kModeStats = 1, kModeLapStats = 2;
constexpr unsigned kPadButton4 = 0xC519BFC3;
constexpr uintptr_t kTicksPerSecond = 0x893718, kRounding = 0x8933D4, kStringFormat = 0x8941F8;  // the floats of the game's Timer(seconds) and the format "%s"

uintptr_t g_get_time_limit = kGetTimeLimit, g_setup = kPostRaceSetup;
void(__fastcall *g_orig_setup_results)(void *, void *);
void(__fastcall *g_orig_notification)(void *, void *, unsigned, void *, unsigned, unsigned);
void(__fastcall *g_orig_setup_lap_stats)(void *, void *, int, void *);
void(__fastcall *g_orig_draw)(void *, void *);
bool(__fastcall *g_orig_compare)(void *, void *, void *);

int &field(void *screen, unsigned offset) { return *reinterpret_cast<int *>(static_cast<unsigned char *>(screen) + offset); }
bool tollbooth_screen(void *screen) { return tollbooth::active() && field(screen, kScreenRaceType) == kRaceTollbooth; }

float __fastcall message_limit(void *parameters, void *edx) {
    return tollbooth::active() ? 0.0f : reinterpret_cast<float(__fastcall *)(void *, void *)>(g_get_time_limit)(parameters, edx);
}

// The screen's constructor sets the booths' mode and runs Setup: here it opens on the list.
void __fastcall first_setup(void *screen, void *edx) {
    if (tollbooth_screen(screen)) field(screen, kScreenMode) = kModeResults;
    reinterpret_cast<void(__fastcall *)(void *, void *)>(g_setup)(screen, edx);
}

void __fastcall hook_setup_results(void *screen, void *edx) {
    const bool as_circuit = tollbooth_screen(screen);
    if (as_circuit) field(screen, kScreenRaceType) = kRaceCircuit;
    g_orig_setup_results(screen, edx);
    if (as_circuit) field(screen, kScreenRaceType) = kRaceTollbooth;
}

void __fastcall hook_notification(void *screen, void *edx, unsigned message, void *object, unsigned a, unsigned b) {
    if (message == kPadButton4 && tollbooth_screen(screen)) {
        int &mode = field(screen, kScreenMode);
        mode = mode == kModeResults ? kModeLapStats : mode == kModeLapStats ? kModeStats : kModeResults;
        reinterpret_cast<void(__fastcall *)(void *, void *)>(g_setup)(screen, edx);
        return;
    }
    g_orig_notification(screen, edx, message, object, a, b);
}

void __fastcall hook_setup_lap_stats(void *screen, void *edx, int racer, void *info) {
    if (!tollbooth_screen(screen)) return g_orig_setup_lap_stats(screen, edx, racer, info);
    tollbooth::Asking asking(racer);
    g_orig_setup_lap_stats(screen, edx, racer, info);
}

// A row of the list: the game prints the racer's race time; a racer who finished gets the time left instead.
void __fastcall hook_draw(void *stat, void *edx) {
    g_orig_draw(stat, edx);
    if (!tollbooth::active()) return;
    const unsigned char *info = *reinterpret_cast<unsigned char **>(static_cast<unsigned char *>(stat) + kStatRacer);
    const int racer = racers::index_of(info);
    if (racer < 0 || !info[racers::kFinished] || info[racers::kKnockedOut] || info[0x1D] || info[0x1E]) return;  // (the flags the game's row checks: totalled, engine blown)
    int ticks = static_cast<int>(tollbooth::time_left(racer) * *reinterpret_cast<float *>(kTicksPerSecond) + *reinterpret_cast<float *>(kRounding));
    char text[32] = {};
    reinterpret_cast<void(__fastcall *)(int *, void *, char *, int)>(kPrintToString)(&ticks, nullptr, text, 0);
    reinterpret_cast<int(__cdecl *)(void *, const char *, ...)>(kFEPrintf)(*reinterpret_cast<void **>(static_cast<unsigned char *>(stat) + kStatData), reinterpret_cast<const char *>(kStringFormat), text);
}

// GRaceStatus::CalculateRankings swaps two neighbours when this is true for (the one ranked first, the other): `a` should come after `b`.
bool __fastcall hook_compare(void *a, void *edx, void *b) {
    if (tollbooth::active()) {
        const unsigned char *x = static_cast<const unsigned char *>(a), *y = static_cast<const unsigned char *>(b);
        const int ra = racers::index_of(x), rb = racers::index_of(y);
        if (ra >= 0 && rb >= 0 && x[racers::kFinished] && y[racers::kFinished]) {
            const float left_a = tollbooth::time_left(ra), left_b = tollbooth::time_left(rb);
            if (left_a != left_b) return left_a < left_b;
        }
    }
    return g_orig_compare(a, edx, b);
}

}  // namespace

void install() {
    const nfsmw::Hook hooks[] = {
        {"tollbooth_list", "PostRaceResultsScreen::SetupResults", kSetupResults, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false, nfsmw::detour(&hook_setup_results),
         nfsmw::orig(&g_orig_setup_results)},
        {"tollbooth_screen_button", "PostRaceResultsScreen::NotificationMessage", kNotificationMessage, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false,
         nfsmw::detour(&hook_notification), nfsmw::orig(&g_orig_notification)},
        {"tollbooth_booth_times", "PostRaceResultsScreen::SetupLapStats", kSetupLapStats, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false, nfsmw::detour(&hook_setup_lap_stats),
         nfsmw::orig(&g_orig_setup_lap_stats)},
        {"tollbooth_row", "RaceResultStat::Draw", kResultStatDraw, {0x83, 0xEC, 0x24, 0x56, 0x8B, 0xF1, 0x8B}, false, nfsmw::detour(&hook_draw), nfsmw::orig(&g_orig_draw)},
        {"tollbooth_ranking", "GRacerInfo ranking comparison", kRankCompare, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x57, 0x8B}, false, nfsmw::detour(&hook_compare),
         nfsmw::orig(&g_orig_compare)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), g_log, g_ini, "tollbooth results");
    if (const uintptr_t previous = nfsmw::chain_call(kMessageLimitCall, reinterpret_cast<uintptr_t>(&message_limit))) {
        g_get_time_limit = previous;
    } else {
        g_log.line("tollbooth: the call at %#x is not a call: a finisher's message says time is up", static_cast<unsigned>(kMessageLimitCall));
    }
    if (const uintptr_t previous = nfsmw::chain_call(kSetupCall, reinterpret_cast<uintptr_t>(&first_setup))) {
        g_setup = previous;
    } else {
        g_log.line("tollbooth: the call at %#x is not a call: the results screen opens on the booths' times", static_cast<unsigned>(kSetupCall));
    }
}

}  // namespace split_screen_support::tollbooth_results

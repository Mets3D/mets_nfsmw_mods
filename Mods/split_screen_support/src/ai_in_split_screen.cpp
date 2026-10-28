/*
 * The Quick Race's AI options in a split screen race: the opponents, their difficulty (AI skill) and catch up rows of the race options, and the opponents
 * themselves. The game leaves the rows out in a split screen game of two (the setup functions of the race types skip them when `[FEDatabase + 0x12C]` says split
 * screen and `[FEDatabase]` is 2) and `Game_InitRacers` (0x61DBE0) does not make the race's opponents when `Sim::GetUserMode` is not 0 (a `jne` at 0x61DC8D,
 * removed here). The race options of Redux v3's NFSMWRaceFixes are built by that plugin's own code (it replaces `UIQRTrackOptions::Setup`), so the rows are
 * not made by changing the game's setup functions: after the screen is built (the constructor's call of Setup, redirected like player_count.cpp does) the rows
 * that are missing are added the way the game adds them: an FEToggleWidget (constructor 0x589300, 0x5C bytes) with the virtual table of the row's class
 * (`NumOpponents` 0x8B779C, `AISkill` 0x8B77D8, `CatchUp` 0x8B7814), handed to `UIWidgetMenu::AddToggleOption` (0x588570), whose Act and Draw change and show
 * the Quick Race settings. Which rows a screen has is noted from the calls of AddToggleOption while it is built. The extra players are put behind the
 * opponents (start_grid.cpp).
 * The race is made when Accept is pressed on the options (`UIQRTrackOptions::NotificationMessage` 0x7AB430, the message 0x406415E3): `FillCustomRace` 0x56DD20
 * copies the Quick Race settings (RaceSettings: laps +4, direction +5, opponents +7, difficulty +8, heat +9, traffic +0xA, catch up +0xB, cops +0xC) into
 * the GRaceCustom and then, in split screen mode, `SetNumOpponents(1)` (the call at 0x7AB500) overrides the opponents. `Game_InitRacers` makes
 * `1 - racers + NumOpponents` opponents (the racers are all the players), so that call is redirected to `Opponents + players - 1`: the number on the row
 * is the number of opponents.
 */
#include "ai_in_split_screen.h"

#include "hooks.h"
#include "patch.h"
#include "player_count.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::ai_in_split_screen {

namespace {

constexpr uintptr_t kFillCustomRace = 0x56DD20, kSetNumOpponentsCall = 0x7AB500, kSetNumOpponents = 0x5DCE60;
constexpr uintptr_t kNew = 0x652AD0, kAddToggleOption = 0x588570, kToggleWidgetCtor = 0x589300, kRefreshWidgets = 0x588D20, kSetupCallSite = 0x7BF73D,
                    kInitRacersOpponents = 0x61DC8D, kGetRaceType = 0x5FAA20;
constexpr unsigned kWidgetSize = 0xB0, kScreenRace = 0x128;  // the largest widget of the Options screens is 0x9D bytes; the screen's GRaceParameters
constexpr uintptr_t kRows[] = {0x8B779C, 0x8B77D8, 0x8B7814};  // NumOpponents, AISkill, CatchUp
constexpr unsigned char kJne[6] = {0x0F, 0x85, 0x8F, 0x00, 0x00, 0x00}, kNops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

// The race types of Setup's jump table (0x7BAA18): 3 and 4 (lap knockout, tollbooth) have none of these rows; every other type has all three.
bool race_has_ai_rows(void *screen) {
    void *race = *reinterpret_cast<void **>(static_cast<char *>(screen) + kScreenRace);
    if (!race) return false;
    const int type = reinterpret_cast<int(__fastcall *)(void *)>(kGetRaceType)(race);
    return type != 3 && type != 4;
}

// ---- the rows a screen got while it was built
void *g_seen[32];
int g_seen_count = 0;
bool g_collecting = false;
unsigned (__fastcall *g_orig_add_toggle)(void *, void *, void *, int);

unsigned __fastcall hook_add_toggle(void *self, void *edx, void *widget, int a) {
    if (g_collecting && g_seen_count < 32) g_seen[g_seen_count++] = *reinterpret_cast<void **>(widget);
    return g_orig_add_toggle(self, edx, widget, a);
}

bool has_row(uintptr_t vtable) {
    for (int i = 0; i < g_seen_count; ++i)
        if (g_seen[i] == reinterpret_cast<void *>(vtable)) return true;
    return false;
}

void add_row(void *screen, uintptr_t vtable) {
    void *widget = reinterpret_cast<void *(*)(size_t)>(kNew)(kWidgetSize);
    if (!widget) return;
    memset(widget, 0, kWidgetSize);
    reinterpret_cast<void *(__fastcall *)(void *, void *, int)>(kToggleWidgetCtor)(widget, nullptr, 1);
    *reinterpret_cast<void **>(widget) = reinterpret_cast<void *>(vtable);
    reinterpret_cast<unsigned(__fastcall *)(void *, void *, void *, int)>(kAddToggleOption)(screen, nullptr, widget, 1);
}

// ---- the screen's constructor calls Setup (or what another mod put there) through this
using SetupFn = void(__fastcall *)(void *, void *);
uintptr_t g_setup = 0x7BA7C0;

void __fastcall setup_and_rows(void *screen, void *edx) {
    g_seen_count = 0;
    g_collecting = true;
    reinterpret_cast<SetupFn>(g_setup)(screen, edx);
    g_collecting = false;
    if (!players::split_screen() || !race_has_ai_rows(screen)) return;
    const int had = g_seen_count;
    int added = 0;
    for (const uintptr_t row : kRows)
        if (!has_row(row)) {
            add_row(screen, row);
            ++added;
        }
    if (added) reinterpret_cast<void(__fastcall *)(void *, void *)>(kRefreshWidgets)(screen, nullptr);  // Draw() on every row: until then the layout's placeholder texts show
    g_log.line("AI in split screen: the race options had %d rows, %d AI rows added", had, added);
}

// ---- the opponents of the race that is made: the row's number (RaceSettings +7) and one more for each player after the first
const unsigned char *g_settings = nullptr;  // the RaceSettings FillCustomRace was last given
void (__fastcall *g_orig_fill)(void *, void *, void *, const unsigned char *);

void __fastcall hook_fill(void *database, void *edx, void *race, const unsigned char *settings) {
    g_settings = settings;
    g_orig_fill(database, edx, race, settings);
}

uintptr_t g_set_num_opponents = kSetNumOpponents;

// What the call that makes the game's split screen races opponent free calls instead (the game's argument is 1).
void __fastcall set_num_opponents(void *race, void *edx, int forced) {
    int opponents = forced;
    if (g_settings) {
        opponents = g_settings[7] + player_count::wanted() - 1;
        g_log.line("AI in split screen: %d opponents and %d players: the race gets %d", g_settings[7], player_count::wanted(), opponents);
    }
    reinterpret_cast<void(__fastcall *)(void *, void *, int)>(g_set_num_opponents)(race, edx, opponents);
}

}  // namespace

void install() {
    const nfsmw::Hook hooks[] = {
        {"aitoggles", "UIWidgetMenu::AddToggleOption", kAddToggleOption, {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x8B}, false, nfsmw::detour(&hook_add_toggle),
         nfsmw::orig(&g_orig_add_toggle)},
    };
    const bool toggles = nfsmw::install_hooks(hooks, 1, g_log, g_ini, "AI in split screen").created == 1;
    const uintptr_t previous = toggles ? nfsmw::chain_call(kSetupCallSite, reinterpret_cast<uintptr_t>(&setup_and_rows)) : 0;
    if (previous) g_setup = previous;
    else g_log.line("AI in split screen: the race options' rows cannot be added (the call at %#x or AddToggleOption is not as expected)", static_cast<unsigned>(kSetupCallSite));
    const nfsmw::Hook fill[] = {
        {"aifill", "cFrontendDatabase::FillCustomRace", kFillCustomRace, {0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x85, 0xFF}, false, nfsmw::detour(&hook_fill), nfsmw::orig(&g_orig_fill)},
    };
    const bool fill_hooked = nfsmw::install_hooks(fill, 1, g_log, g_ini, "AI in split screen").created == 1;
    if (const uintptr_t target = fill_hooked ? nfsmw::chain_call(kSetNumOpponentsCall, reinterpret_cast<uintptr_t>(&set_num_opponents)) : 0) g_set_num_opponents = target;
    else g_log.line("AI in split screen: the call at %#x is not as expected: a split screen race has no opponents", static_cast<unsigned>(kSetNumOpponentsCall));
    const bool opponents = nfsmw::patch_bytes(kInitRacersOpponents, kJne, kNops, sizeof(kNops));
    g_log.line("AI in split screen: rows %s, opponents %s", previous ? "on" : "off", opponents ? "on" : "off (Game_InitRacers is not as expected)");
}

}  // namespace split_screen_support::ai_in_split_screen

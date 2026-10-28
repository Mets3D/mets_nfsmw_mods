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
 * The Opponents row is not the one a split screen race gets: a "Total Racers" row (the players and the opponents together, from the number of players up to 16, which is what
 * GRaceStatus has room for) takes its place, since the number of opponents that fits depends on the number of players. The race then gets `SetNumOpponents(total - 1)`
 * (`Game_InitRacers` makes `1 - racers + NumOpponents` opponents, the racers being the players). Without the row's texts (nfsmw_custom_menus) the Opponents row is added.
 * Two things keep such a race from crashing: the race context of GRaceStatus (+0x1964: 0 Quick Race, 1 online, 2 career) is the career's after a race or from the start of
 * the game and the Quick Race flow sets it when the race options are accepted; a split screen race started some other way (the game went from the Quick Race menu to the
 * Press Start screens without the options) ran `Game_InitRacers` in the career context and crashed in `AddRacer` (`ChooseRacerName` of an opponent with no character):
 * `prepare_race` sets the Quick Race context. And `AddRacer` (0x601F90) ignores a missing opponent character (`GetOpponentChar` 0x5FD2D0 returns none for an index
 * past the end of the race's list of opponents, which a race of many racers can ask for). The same race had a number of opponents (`GetNumOpponents` 0x5FACD0) below the
 * number of players, so `1 - racers + NumOpponents` was negative and the loop of `Game_InitRacers` (`cmp edi, ebp; jb`: unsigned) ran four billion times: the number is
 * raised to the players minus one for that call (and to the Total Racers row's when the race did not come through its row).
 */
#include "ai_in_split_screen.h"

#include <algorithm>

#include "hooks.h"
#include "option_row.h"
#include "patch.h"
#include "player_count.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::ai_in_split_screen {

namespace {

constexpr uintptr_t kFillCustomRace = 0x56DD20, kSetNumOpponentsCall = 0x7AB500, kSetNumOpponents = 0x5DCE60;
constexpr uintptr_t kAddToggleOption = option_row::kAddToggleOption, kSetupCallSite = 0x7BF73D, kInitRacersOpponents = 0x61DC8D, kGetRaceType = 0x5FAA20;
constexpr unsigned kScreenRace = 0x128;  // the screen's GRaceParameters
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

void add_row(void *screen, uintptr_t vtable) { option_row::add(screen, reinterpret_cast<void **>(vtable)); }

// ---- the Total Racers row: in a split screen race it takes the place of the Opponents row
constexpr int kMaxRacers = 16;        // GRaceStatus has room for 16 racers (0x194 bytes each from +0x18, the count at +0x1958)
constexpr int kDefaultOpponents = 3;  // the Quick Race's: the total is the players and this many until the row is changed
const unsigned kTotalTitleHash = nfsmw::bstring_hash("SPLIT_TOTAL_RACERS");

unsigned total_hash(int racers) {
    char name[32];
    snprintf(name, sizeof(name), "SPLIT_TOTAL_RACERS_%d", racers);
    return nfsmw::bstring_hash(name);
}

int g_total = 0;  // the total the row was set to; 0 until it was
bool g_total_texts = false;
bool g_total_row = false;  // the race options that were built last have the row
void *g_total_widget = nullptr;
void *g_total_vtable[option_row::kSlots];

int total_racers() {
    const int players = player_count::wanted();
    return std::clamp(g_total ? g_total : players + kDefaultOpponents, players, kMaxRacers);
}

void __fastcall total_draw(void *self, void *) { option_row::set_texts(self, kTotalTitleHash, total_hash(total_racers())); }

void __fastcall total_act(void *self, void *, const char *, unsigned data) {
    if (data != option_row::kPadLeft && data != option_row::kPadRight) return;
    const int players = player_count::wanted(), total = total_racers();
    g_total = data == option_row::kPadRight ? (total >= kMaxRacers ? players : total + 1) : (total <= players ? kMaxRacers : total - 1);
    option_row::value_changed(self, data);
    g_log.line("race options: %d racers in all (%d players)", g_total, players);
}

// ---- the screen's constructor calls Setup (or what another mod put there) through this
using SetupFn = void(__fastcall *)(void *, void *);
uintptr_t g_setup = 0x7BA7C0;

void __fastcall setup_and_rows(void *screen, void *edx) {
    g_seen_count = 0;
    g_total_row = false;
    g_total_widget = nullptr;  // (the screen that had it is gone)
    g_collecting = true;
    reinterpret_cast<SetupFn>(g_setup)(screen, edx);
    g_collecting = false;
    if (!players::split_screen() || !race_has_ai_rows(screen)) return;
    const int had = g_seen_count;
    int added = 0;
    for (const uintptr_t row : kRows)
        if (!has_row(row)) {
            if (row == kRows[0] && g_total_texts) {
                g_total_widget = option_row::add(screen, g_total_vtable);
                g_total_row = g_total_widget != nullptr;
            } else {
                add_row(screen, row);
            }
            ++added;
        }
    if (added) option_row::refresh(screen);
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

void (__fastcall *g_orig_add_racer)(void *, void *, void *);
int g_left_out = 0;

// GRaceStatus::AddRacer(GRuntimeInstance *): nothing to add when the opponent has no character.
void __fastcall hook_add_racer(void *status, void *edx, void *character) {
    if (!character) {
        if (g_left_out++ < 3) g_log.line("AI in split screen: no opponent character for a racer: left out");
        return;
    }
    g_orig_add_racer(status, edx, character);
}

constexpr uintptr_t kRaceStatusPointer = 0x91E000, kGetRacerCount = 0x5DBEF0;
bool g_making_racers = false;  // Game_InitRacers is running
int (__fastcall *g_orig_get_num_opponents)(void *);

// GRaceParameters::GetNumOpponents: what Game_InitRacers adds to 1 minus the number of racers (the players) to get the number of opponents it makes.
int __fastcall hook_get_num_opponents(void *self, void *) {
    const int numbered = g_orig_get_num_opponents(self);
    if (!g_making_racers || !players::split_screen()) return numbered;
    void *status = *reinterpret_cast<void **>(kRaceStatusPointer);
    const int racers = status ? reinterpret_cast<int(__thiscall *)(void *)>(kGetRacerCount)(status) : 0;
    if (numbered >= racers - 1) return numbered;
    const int raised = (std::max)(total_racers() - 1, racers - 1);
    g_log.line("AI in split screen: the race has %d opponents for %d players: raised to %d", numbered, racers, raised);
    return raised;
}

// What the call that makes the game's split screen races opponent free calls instead (the game's argument is 1).
void __fastcall set_num_opponents(void *race, void *edx, int forced) {
    int opponents = forced;
    if (g_settings) {
        opponents = g_total_row ? total_racers() - 1 : g_settings[7] + player_count::wanted() - 1;
        g_log.line("AI in split screen: %d players, %s: the race gets %d", player_count::wanted(), g_total_row ? "total racers row" : "opponents row", opponents);
    }
    reinterpret_cast<void(__fastcall *)(void *, void *, int)>(g_set_num_opponents)(race, edx, opponents);
}

}  // namespace

void prepare_race() {
    constexpr uintptr_t kRaceStatus = 0x91E000;
    constexpr unsigned kRaceContext = 0x1964;
    unsigned char *status = *reinterpret_cast<unsigned char **>(kRaceStatus);
    g_left_out = 0;
    g_making_racers = true;
    if (!status || !players::split_screen()) return;
    int &context = *reinterpret_cast<int *>(status + kRaceContext);
    if (context == 0) return;
    g_log.line("AI in split screen: the race context was %d: set to the Quick Race's", context);
    context = 0;
}

void racers_made() { g_making_racers = false; }

void players_changed() {
    if (g_total && g_total < player_count::wanted()) g_total = player_count::wanted();  // (the total is never below the players)
    if (g_total_row && g_total_widget) total_draw(g_total_widget, nullptr);
}

void register_texts(const nfsmw::custom_menus::Host *host) {
    if (!ini_flag("options", "ai_in_split_screen", true)) return;
    host->register_text(kTotalTitleHash, "Total Racers");
    static char texts[kMaxRacers + 1][8];  // the host keeps the pointers
    for (int racers = 2; racers <= kMaxRacers; ++racers) {
        snprintf(texts[racers], sizeof(texts[racers]), "%d", racers);
        host->register_text(total_hash(racers), texts[racers]);
    }
    g_total_texts = true;
}

void install() {
    option_row::make_vtable(g_total_vtable, &total_act, &total_draw);
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
    const nfsmw::Hook add_racer[] = {
        {"aiaddracer", "GRaceStatus::AddRacer", 0x601F90, {0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x87, 0x58}, false, nfsmw::detour(&hook_add_racer), nfsmw::orig(&g_orig_add_racer)},
    };
    nfsmw::install_hooks(add_racer, 1, g_log, g_ini, "AI in split screen");
    const nfsmw::Hook num_opponents[] = {
        {"ainumopponents", "GRaceParameters::GetNumOpponents", 0x5FACD0, {0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x0C, 0x85}, false, nfsmw::detour(&hook_get_num_opponents),
         nfsmw::orig(&g_orig_get_num_opponents)},
    };
    nfsmw::install_hooks(num_opponents, 1, g_log, g_ini, "AI in split screen");
    const bool fill_hooked = nfsmw::install_hooks(fill, 1, g_log, g_ini, "AI in split screen").created == 1;
    if (const uintptr_t target = fill_hooked ? nfsmw::chain_call(kSetNumOpponentsCall, reinterpret_cast<uintptr_t>(&set_num_opponents)) : 0) g_set_num_opponents = target;
    else g_log.line("AI in split screen: the call at %#x is not as expected: a split screen race has no opponents", static_cast<unsigned>(kSetNumOpponentsCall));
    const bool opponents = nfsmw::patch_bytes(kInitRacersOpponents, kJne, kNops, sizeof(kNops));
    g_log.line("AI in split screen: rows %s, opponents %s", previous ? "on" : "off", opponents ? "on" : "off (Game_InitRacers is not as expected)");
}

}  // namespace split_screen_support::ai_in_split_screen

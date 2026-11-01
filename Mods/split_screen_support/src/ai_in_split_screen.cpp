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
 * The game's Opponents row (1 to 3) is not the one a split screen race gets: an "AI Opponents" row of ours (0 up to `[options] max_racers` minus the players; GRaceStatus has room for 16 racers)
 * takes its place, and a change of the Players row lowers it to what fits. The race then gets `SetNumOpponents(opponents + players - 1)`. Without the row's texts
 * (nfsmw_custom_menus) the game's Opponents row is added.
 * Every race type gets the rows: Redux v3's options of the knockout and the tollbooth have none in split screen, and a race without the row got the opponents of the
 * single player Quick Race (RaceSettings +7).
 * A lap knockout knocks out one racer per lap, so it has racers - 1 laps (the game's Opponents row set the laps to the opponents, and its Laps row is greyed out): the
 * Laps row (the NumLaps widget, vtable 0x8B788C) is greyed out here and shows that number, and the race gets it (`SetNumLaps` 0x5E9A70).
 * Two things keep such a race from crashing: the race context of GRaceStatus (+0x1964: 0 Quick Race, 1 online, 2 career) is the career's after a race or from the start of
 * the game and the Quick Race flow sets it when the race options are accepted; a split screen race started some other way (the game went from the Quick Race menu to the
 * Press Start screens without the options) ran `Game_InitRacers` in the career context and crashed in `AddRacer` (`ChooseRacerName` of an opponent with no character):
 * `prepare_race` sets the Quick Race context. And `AddRacer` (0x601F90) ignores a missing opponent character (`GetOpponentChar` 0x5FD2D0 returns none for an index
 * past the end of the race's list of opponents, which a race of many racers can ask for). The same race had a number of opponents (`GetNumOpponents` 0x5FACD0) below the
 * number of players, so `1 - racers + NumOpponents` was negative and the loop of `Game_InitRacers` (`cmp edi, ebp; jb`: unsigned) ran four billion times: the number is
 * raised to the players minus one for that call (and to the AI Opponents row's when the race did not come through its row).
 */
#include "ai_in_split_screen.h"

#include <algorithm>
#include <array>
#include <vector>

#include "hooks.h"
#include "option_row.h"
#include "patch.h"
#include "player_count.h"
#include "racers.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::ai_in_split_screen {

namespace {

constexpr uintptr_t kFillCustomRace = 0x56DD20, kSetNumOpponentsCall = 0x7AB500, kSetNumOpponents = 0x5DCE60;
constexpr uintptr_t kAddToggleOption = option_row::kAddToggleOption, kSetupCallSite = 0x7BF73D, kInitRacersOpponents = 0x61DC8D, kGetRaceType = 0x5FAA20, kSetNumLaps = 0x5E9A70;
constexpr uintptr_t kFeDatabase = 0x91CF90, kGetQuickRaceSettings = 0x56DBE0, kNumLapsVtable = 0x8B788C, kDisableWidget = 0x589440, kEnableWidget = 0x589410;
constexpr unsigned kScreenRace = 0x128;  // the screen's GRaceParameters
constexpr int kRaceKnockout = 3, kAllTypes = 11;  // GRace::Type
constexpr unsigned kSettingsLaps = 4;             // RaceSettings: laps
constexpr uintptr_t kRows[] = {0x8B779C, 0x8B77D8, 0x8B7814};  // NumOpponents, AISkill, CatchUp
constexpr unsigned char kJne[6] = {0x0F, 0x85, 0x8F, 0x00, 0x00, 0x00}, kNops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

// ---- the rows a screen got while it was built
void *g_seen[32];
int g_seen_count = 0;
bool g_collecting = false;
void *g_laps_widget = nullptr;  // the Laps row of the screen that was built last
void *g_skill_widget = nullptr, *g_catch_up_widget = nullptr;  // its Difficulty and Catch Up rows
unsigned (__fastcall *g_orig_add_toggle)(void *, void *, void *, int);

unsigned __fastcall hook_add_toggle(void *self, void *edx, void *widget, int a) {
    if (g_collecting && g_seen_count < 32) g_seen[g_seen_count++] = *reinterpret_cast<void **>(widget);
    if (g_collecting && *reinterpret_cast<uintptr_t *>(widget) == kNumLapsVtable) g_laps_widget = widget;
    if (g_collecting && *reinterpret_cast<uintptr_t *>(widget) == kRows[1]) g_skill_widget = widget;
    if (g_collecting && *reinterpret_cast<uintptr_t *>(widget) == kRows[2]) g_catch_up_widget = widget;
    return g_orig_add_toggle(self, edx, widget, a);
}

bool has_row(uintptr_t vtable) {
    for (int i = 0; i < g_seen_count; ++i)
        if (g_seen[i] == reinterpret_cast<void *>(vtable)) return true;
    return false;
}

void add_row(void *screen, uintptr_t vtable) {
    void *widget = option_row::add(screen, reinterpret_cast<void **>(vtable));
    if (vtable == kRows[1]) g_skill_widget = widget;
    if (vtable == kRows[2]) g_catch_up_widget = widget;
}

// ---- the AI Opponents row: in a split screen race it takes the place of the Opponents row
constexpr int kDefaultOpponents = 3;  // the Quick Race's, until the row is changed
const unsigned kOpponentsTitleHash = nfsmw::bstring_hash("SPLIT_AI_OPPONENTS");

unsigned opponents_hash(int opponents) {
    char name[32];
    snprintf(name, sizeof(name), "SPLIT_AI_OPPONENTS_%d", opponents);
    return nfsmw::bstring_hash(name);
}

int g_opponents = kDefaultOpponents;
bool g_opponents_texts = false;
bool g_opponents_row = false;  // the race options that were built last have the row
void *g_opponents_widget = nullptr;
void *g_opponents_vtable[option_row::kSlots];

int max_opponents() {
    static const int racers_most = racers::max_racers(g_ini);
    return racers_most - player_count::wanted();
}
int ai_opponents() { return std::clamp(g_opponents, 0, max_opponents()); }

bool g_knockout_screen = false;  // the race options that were built last are a lap knockout's

// A lap knockout has one lap for each racer but the winner: the Laps row (greyed out, as the game's) shows it, and the race gets it (set_num_opponents).
void sync_knockout_laps() {
    if (!g_knockout_screen || !g_laps_widget) return;
    const int laps = ai_opponents() + player_count::wanted() - 1;
    void *settings = reinterpret_cast<void *(__fastcall *)(void *, void *, int)>(kGetQuickRaceSettings)(*reinterpret_cast<void **>(kFeDatabase), nullptr, kAllTypes);
    if (!settings) return;
    *(static_cast<unsigned char *>(settings) + kSettingsLaps) = static_cast<unsigned char>(laps);
    reinterpret_cast<void(__fastcall *)(void *, void *)>(kDisableWidget)(g_laps_widget, nullptr);
    option_row::redraw(g_laps_widget);
}

// Without opponents the Difficulty and Catch Up rows do nothing: greyed out (the widget's Disable, as the game's Laps row), and back when there are some. The rows are
// only touched when that changes: Enable plays the row's unhighlight script again, which left the rows' values dimmer than the other rows' when it was done to rows that had
// never been disabled.
bool g_ai_rows_disabled = false;

void sync_ai_rows() {
    if (!g_opponents_row || (ai_opponents() == 0) == g_ai_rows_disabled) return;
    g_ai_rows_disabled = ai_opponents() == 0;
    for (void *widget : {g_skill_widget, g_catch_up_widget}) {
        if (!widget) continue;
        reinterpret_cast<void(__fastcall *)(void *, void *)>(ai_opponents() == 0 ? kDisableWidget : kEnableWidget)(widget, nullptr);
        option_row::redraw(widget);
    }
}

void sync_rows() {
    sync_knockout_laps();
    sync_ai_rows();
}

void __fastcall opponents_draw(void *self, void *) { option_row::set_texts(self, kOpponentsTitleHash, opponents_hash(ai_opponents())); }

void __fastcall opponents_act(void *self, void *, const char *, unsigned data) {
    if (data != option_row::kPadLeft && data != option_row::kPadRight) return;
    const int most = max_opponents(), opponents = ai_opponents();
    g_opponents = data == option_row::kPadRight ? (opponents >= most ? 0 : opponents + 1) : (opponents <= 0 ? most : opponents - 1);
    option_row::value_changed(self, data);
    g_log.line("race options: %d AI opponents (%d players)", g_opponents, player_count::wanted());
    sync_rows();
}

// ---- the screen's constructor calls Setup (or what another mod put there) through this
using SetupFn = void(__fastcall *)(void *, void *);
uintptr_t g_setup = 0x7BA7C0;

void __fastcall setup_and_rows(void *screen, void *edx) {
    g_seen_count = 0;
    g_opponents_row = false;
    g_opponents_widget = nullptr;  // (the screen that had it is gone)
    g_laps_widget = g_skill_widget = g_catch_up_widget = nullptr;
    g_ai_rows_disabled = false;
    g_knockout_screen = false;
    g_collecting = true;
    reinterpret_cast<SetupFn>(g_setup)(screen, edx);
    g_collecting = false;
    if (!players::split_screen()) return;
    void *race = *reinterpret_cast<void **>(static_cast<char *>(screen) + kScreenRace);
    g_knockout_screen = race && reinterpret_cast<int(__fastcall *)(void *)>(kGetRaceType)(race) == kRaceKnockout;
    const int had = g_seen_count;
    int added = 0;
    for (const uintptr_t row : kRows)
        if (!has_row(row)) {
            if (row == kRows[0] && g_opponents_texts) {
                g_opponents_widget = option_row::add(screen, g_opponents_vtable);
                g_opponents_row = g_opponents_widget != nullptr;
            } else {
                add_row(screen, row);
            }
            ++added;
        }
    if (added) option_row::refresh(screen);
    sync_rows();
    g_log.line("AI in split screen: the race options had %d rows, %d AI rows added%s", had, added,
               !g_knockout_screen ? "" : g_laps_widget ? "; lap knockout: the Laps row follows the racers" : "; lap knockout: NO Laps row found");
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
    const int raised = (std::max)(ai_opponents() + player_count::wanted() - 1, racers - 1);
    g_log.line("AI in split screen: the race has %d opponents for %d players: raised to %d", numbered, racers, raised);
    return raised;
}

// What the call that makes the game's split screen races opponent free calls instead (the game's argument is 1).
void __fastcall set_num_opponents(void *race, void *edx, int forced) {
    int opponents = forced;
    if (g_settings) {
        opponents = (g_opponents_row ? ai_opponents() : g_settings[7]) + player_count::wanted() - 1;
        g_log.line("AI in split screen: %d players, %s: the race gets %d", player_count::wanted(), g_opponents_row ? "AI opponents row" : "opponents row", opponents);
        if (reinterpret_cast<int(__fastcall *)(void *)>(kGetRaceType)(race) == kRaceKnockout) {
            reinterpret_cast<void(__fastcall *)(void *, void *, int)>(kSetNumLaps)(race, edx, opponents);  // (one racer out per lap: the winner is the one left)
            g_log.line("AI in split screen: lap knockout: %d laps", opponents);
        }
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
    g_opponents = ai_opponents();  // (the players and the opponents are never more than max_racers)
    if (g_opponents_row && g_opponents_widget) opponents_draw(g_opponents_widget, nullptr);
    sync_rows();
}

void register_texts(const nfsmw::custom_menus::Host *host) {
    if (!ini_flag("options", "enable_ai_opponents_in_split_screen", true)) return;
    host->register_text(kOpponentsTitleHash, "AI Opponents");
    static std::vector<std::array<char, 8>> texts(racers::max_racers(g_ini));  // the host keeps the pointers
    for (int opponents = 0; opponents < static_cast<int>(texts.size()); ++opponents) {
        snprintf(texts[opponents].data(), texts[opponents].size(), "%d", opponents);
        host->register_text(opponents_hash(opponents), texts[opponents].data());
    }
    g_opponents_texts = true;
}

// GRacerInfo::CreateVehicle(default car key) 0x5E8E60 makes a racer's car from the racer's character (its attributes 0xF833C06F, the name of the car, and 0x416A8409, the name
// of a preset ride) and gives 0 when it cannot: the racer then has no car at all (the restart log's "(no body)"). The failure is logged, the names with it.
constexpr uintptr_t kCreateVehicle = 0x5E8E60, kGetAttributePointer = 0x454810;
constexpr unsigned kCharacterCar = 0xF833C06F, kCharacterPreset = 0x416A8409;
void *(__fastcall *g_orig_create_vehicle)(void *, void *, unsigned);

const char *character_text(void *racer_info, unsigned key, char *out, size_t size) {
    snprintf(out, size, "?");
    void *character = *reinterpret_cast<void **>(static_cast<char *>(racer_info) + 4);
    if (!character) return out;
    const void *const *attribute = static_cast<const void *const *>(
        reinterpret_cast<const void *(__fastcall *)(void *, void *, unsigned, unsigned)>(kGetAttributePointer)(static_cast<char *>(character) + 4, nullptr, key, 0));
    const char *text = attribute ? static_cast<const char *>(*attribute) : nullptr;
    if (text && !IsBadReadPtr(text, 1)) snprintf(out, size, "%.30s", text);
    else snprintf(out, size, "-");
    return out;
}

void *__fastcall hook_create_vehicle(void *racer_info, void *edx, unsigned default_car) {
    void *vehicle = g_orig_create_vehicle(racer_info, edx, default_car);
    if (vehicle) return vehicle;
    const unsigned char *status = *reinterpret_cast<unsigned char **>(kRaceStatusPointer);
    const long index = status ? (static_cast<unsigned char *>(racer_info) - (status + 0x18)) / 0x194 : -1;
    char car[32], preset[32];
    const char *has_character = *reinterpret_cast<void **>(static_cast<char *>(racer_info) + 4) ? "" : " (no character)";
    g_log.line("racer %ld: NO CAR MADE (car \"%s\", preset \"%s\"%s)", index + 1, character_text(racer_info, kCharacterCar, car, sizeof(car)),
               character_text(racer_info, kCharacterPreset, preset, sizeof(preset)), has_character);
    return vehicle;
}

void install() {
    option_row::make_vtable(g_opponents_vtable, &opponents_act, &opponents_draw);
    const nfsmw::Hook create_vehicle[] = {
        {"aicreatevehicle", "GRacerInfo::CreateVehicle", kCreateVehicle, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false, nfsmw::detour(&hook_create_vehicle),
         nfsmw::orig(&g_orig_create_vehicle)},
    };
    nfsmw::install_hooks(create_vehicle, 1, g_log, g_ini, "AI in split screen");
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

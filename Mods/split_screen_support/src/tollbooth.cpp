/*
 * The clocks of a tollbooth race. The game has one: `GRaceStatus::GetRaceTimeRemaining` (0x5FE090) is the race's time limit plus the time bonuses (+0x1980) minus the time
 * that has passed, and the race's script awards a booth's bonus with the Lua binding `AwardBonusTime` (0x5FE600) when a racer for whom `ISimable::IsPlayer` is true crosses it,
 * so in a split screen race every booth any player crossed gave all of them time. Here every racer has a clock of their own:
 *   - `AwardBonusTime` adds the bonus to the racer who crossed (`Game_NotifyCheckpointReached` 0x6117F0, which the script calls with the racer just before, tells who) and
 *     writes down the time left at the booth (GRacerInfo +0x30 the booths crossed, +0x34 the times) for that racer only;
 *   - `GetRaceTimeRemaining` answers for the racer the game is asking about: the player whose HUD is updating (`LocalPlayer::UpdateHud` 0x6F13F0, `TimeExtension::Update`
 *     0x57B780) or who crosses; asked about nobody (the race's own check that the time is up, `GRaceStatus::Update`) it answers with the most time any player still in the
 *     race has, so the race is over when the last of them is out;
 *   - a racer whose time is up is knocked out as in a lap knockout (`Game_KnockoutRacer` 0x611440: knockout.cpp stops a player's car and posts the game's message);
 *   - the "+ seconds" message (`HUD.ShowTimeExtension` 0x6113E0 always shows it to the first player) goes to the player who crossed;
 *   - the opponents have clocks too: `IsPlayer` is true for them, in a tollbooth race, so that the script awards them the bonus;
 *   - a racer's clock stops at the finish, and the HUD's "Hurry!" warning (`TimeExtension::SetPlayerLapTime`) is silent for a racer who finished or is out.
 * The ranking and the results screen are tollbooth_results.cpp. The parts can be left out one by one with the `[hooks]` list of the ini.
 */
#include "tollbooth.h"

#include <algorithm>
#include <vector>

#include "hooks.h"
#include "max_players.h"
#include "more_players.h"
#include "patch.h"
#include "players.h"
#include "racers.h"
#include "split_screen_support.h"

namespace split_screen_support::tollbooth {

namespace {

constexpr uintptr_t kGetRaceTimeRemaining = 0x5FE090, kAwardBonusTime = 0x5FE600, kNotifyCheckpointReached = 0x6117F0, kResetRaceTimers = 0x5FFD80, kShowTimeExtension = 0x6113E0,
                    kUpdateHud = 0x6F13F0, kTimeExtensionUpdate = 0x57B780, kIsPlayer = 0x609F10, kTickCall = 0x602EB9, kSetPlayerLapTime = 0x5A0100;
constexpr uintptr_t kIsTollboothRace = 0x50E1B0, kGetTimeLimit = 0x5FC260, kGetRaceTimeElapsed = 0x5E3060, kKnockoutRacer = 0x611440, kEventNew = 0x627400,
                    kShowTimeExtensionEvent = 0x6252D0;
constexpr unsigned kRaceParameters = 0x1968, kBooths = 0x30, kBoothTimes = 0x34, kMaxBooths = 16;  // GRaceStatus; GRacerInfo
constexpr unsigned kLocalPlayerInterface = 0x2C, kInterfaceGetSimable = 3;                         // LocalPlayer::UpdateHud asks the object at this + 0x2C (virtual slot 3)

struct Clock {
    float bonus = 0;           // the time the racer has been awarded
    bool timed_out = false;
    bool has_frozen = false;   // the racer finished: they have no clock any more, only the time they had left
    float frozen = 0;
};
std::vector<Clock> g_clocks;  // one for each racer of the race

Clock &clock_of(int racer) {
    if (static_cast<size_t>(racer) >= g_clocks.size()) g_clocks.resize(racer + 1);
    return g_clocks[racer];
}

int g_crossing = -1;  // the racer who crossed the booth the script is dealing with
int g_context = -1;   // the racer the game is asking the time of (-1: nobody in particular)

float(__fastcall *g_orig_remaining)(void *, void *);
void(__fastcall *g_orig_award)(void *, void *, float);
void(__cdecl *g_orig_notify_checkpoint)(void *, int);
void(__fastcall *g_orig_reset_timers)(void *, void *);
void(__cdecl *g_orig_show_time_extension)(float);
void(__fastcall *g_orig_update_hud)(void *, void *, float);
void(__fastcall *g_orig_time_extension_update)(void *, void *, void *);
char(__fastcall *g_orig_is_player)(void *, void *);
void(__fastcall *g_orig_set_lap_time)(void *, void *, float);

// The player (0-based) of a racer, -1 for an opponent.
int player_of_racer(int racer) {
    for (int i = 0; i < nfsmw::kMaxPlayers; ++i)
        if (racers::of_player(more_players::game_player(i)) == racer && racer >= 0) return i;
    return -1;
}

bool is_human(int racer) { return player_of_racer(racer) >= 0; }

float time_limit(void *status) {
    void *parameters = *reinterpret_cast<void **>(static_cast<unsigned char *>(status) + kRaceParameters);
    return parameters ? reinterpret_cast<float(__fastcall *)(void *, void *)>(kGetTimeLimit)(parameters, nullptr) : 0.0f;
}

// What the racer has left: the limit and what they were awarded, less the time that has passed; the same from the finish on.
float time_left(void *status, int racer) {
    const float limit = time_limit(status);
    if (limit <= 0.0f || racer < 0 || racer >= racers::count()) return 0.0f;
    Clock &clock = clock_of(racer);
    if (clock.has_frozen) return clock.frozen;
    const float left = limit + clock.bonus - reinterpret_cast<float(__fastcall *)(void *, void *)>(kGetRaceTimeElapsed)(status, nullptr);
    const float live = left > 0.0f ? left : 0.0f;
    const unsigned char *info = racers::info(racer);
    if (info && info[racers::kFinished]) {
        clock.has_frozen = true;
        clock.frozen = live;
    }
    return live;
}

bool knocked_out(int racer) {
    const unsigned char *info = racers::info(racer);
    return !info || info[racers::kKnockedOut];
}

float __fastcall hook_remaining(void *status, void *edx) {
    if (!active() || status != racers::status()) return g_orig_remaining(status, edx);
    if (g_context >= 0) return time_left(status, g_context);
    float most = 0.0f;
    bool any_player = false;
    for (int i = 0; i < nfsmw::kMaxPlayers; ++i) {
        const int racer = racers::of_player(more_players::game_player(i));
        if (racer < 0) continue;
        any_player = true;
        if (!knocked_out(racer)) most = std::max(most, time_left(status, racer));
    }
    return any_player ? most : g_orig_remaining(status, edx);
}

// Every frame of a race (in place of the call that posts the time left): the racers whose time is up are knocked out.
float __fastcall tick(void *status, void *edx) {
    if (active() && time_limit(status) > 0.0f) {
        for (int i = 0; i < racers::count(); ++i) {
            const unsigned char *info = racers::info(i);
            if (info && info[racers::kFinished]) time_left(status, i);  // (stops the clock of a racer who finished)
            if (!info || info[racers::kKnockedOut] || info[racers::kFinished] || clock_of(i).timed_out) continue;
            if (time_left(status, i) > 0.0f) continue;
            void *simable = racers::simable(info);
            if (!simable) continue;
            clock_of(i).timed_out = true;
            g_log.line("tollbooth: racer %d (%s) is out of time", i + 1, is_human(i) ? "a player" : "an opponent");
            reinterpret_cast<void(*)(void *)>(kKnockoutRacer)(simable);
        }
    }
    return hook_remaining(status, edx);
}

void __fastcall hook_reset_timers(void *status, void *edx) {
    g_orig_reset_timers(status, edx);
    g_clocks.assign(racers::count(), Clock());
    g_crossing = -1;
}

// The script tells the game about a racer reaching a checkpoint and then (for a booth, and a racer that `IsPlayer`) awards the time: the racer is remembered for that.
void __cdecl hook_notify_checkpoint(void *simable, int goal) {
    g_crossing = active() ? racers::index_of_simable(simable) : -1;
    g_orig_notify_checkpoint(simable, goal);
}

void __fastcall hook_award(void *status, void *edx, float bonus) {
    unsigned char *info = active() && g_crossing >= 0 ? racers::info(g_crossing) : nullptr;
    if (!info) return g_orig_award(status, edx, bonus);
    int &booths = *reinterpret_cast<int *>(info + kBooths);
    if (booths >= 0 && booths < static_cast<int>(kMaxBooths)) *reinterpret_cast<float *>(info + kBoothTimes + booths * 4) = time_left(status, g_crossing);
    ++booths;
    clock_of(g_crossing).bonus += bonus;
    g_log.line("tollbooth: racer %d crosses booth %d: +%.1f s, %.1f s left", g_crossing + 1, booths, bonus, time_left(status, g_crossing));
}

// HUD.ShowTimeExtension(seconds): the game shows the message to the first player; it goes to the one who crossed (nobody when it was an opponent).
void __cdecl hook_show_time_extension(float seconds) {
    if (!active() || g_crossing < 0) return g_orig_show_time_extension(seconds);
    const int player = player_of_racer(g_crossing);
    if (player < 0) return;
    void *event = reinterpret_cast<void *(*)(unsigned)>(kEventNew)(0x10);
    if (event) reinterpret_cast<void *(__fastcall *)(void *, void *, void *, float)>(kShowTimeExtensionEvent)(event, nullptr, more_players::game_player(player), seconds);
}

void __fastcall hook_update_hud(void *player, void *edx, float dt) {
    const int context = g_context;
    g_context = -1;
    if (active()) {
        void *part = static_cast<unsigned char *>(player) + kLocalPlayerInterface;
        void *simable = reinterpret_cast<void *(__fastcall *)(void *, void *)>((*reinterpret_cast<void ***>(part))[kInterfaceGetSimable])(part, nullptr);
        g_context = racers::index_of_simable(simable);
    }
    g_orig_update_hud(player, edx, dt);
    g_context = context;
}

void __fastcall hook_time_extension_update(void *self, void *edx, void *player) {
    const int context = g_context;
    g_context = active() ? racers::of_player(player) : -1;
    g_orig_time_extension_update(self, edx, player);
    g_context = context;
}

// The HUD's "Hurry!" warning follows the time the HUD is told (TimeExtension::SetPlayerLapTime): a racer who has finished or is out has none.
void __fastcall hook_set_lap_time(void *self, void *edx, float seconds) {
    const unsigned char *info = active() && g_context >= 0 ? racers::info(g_context) : nullptr;
    if (info && (info[racers::kFinished] || info[racers::kKnockedOut])) seconds = 0.0f;
    g_orig_set_lap_time(self, edx, seconds);
}

// Lua's `Element:IsPlayer()` is what the script checks before it awards time.
char __fastcall hook_is_player(void *simable, void *edx) {
    const char is_player = g_orig_is_player(simable, edx);
    if (is_player || !active()) return is_player;
    const int racer = racers::index_of_simable(simable);
    return racer >= 0;
}

}  // namespace

bool active() { return players::split_screen() && reinterpret_cast<bool (*)()>(kIsTollboothRace)(); }

float time_left(int racer) { return time_left(racers::status(), racer); }

Asking::Asking(int racer) : previous_(g_context) { g_context = racer; }
Asking::~Asking() { g_context = previous_; }

void install() {
    const nfsmw::Hook hooks[] = {
        {"tollbooth_remaining", "GRaceStatus::GetRaceTimeRemaining", kGetRaceTimeRemaining, {0x51, 0x56, 0x8B, 0xF1, 0x8B, 0x8E, 0x68}, false, nfsmw::detour(&hook_remaining),
         nfsmw::orig(&g_orig_remaining)},
        {"tollbooth_award", "GRaceStatus::AwardBonusTime", kAwardBonusTime, {0x53, 0x8B, 0xD9, 0x8B, 0x83, 0x58, 0x19}, false, nfsmw::detour(&hook_award), nfsmw::orig(&g_orig_award)},
        {"tollbooth_checkpoint", "Game_NotifyCheckpointReached", kNotifyCheckpointReached, {0x51, 0x8B, 0x44, 0x24, 0x08, 0x56, 0x57}, false,
         nfsmw::detour(&hook_notify_checkpoint), nfsmw::orig(&g_orig_notify_checkpoint)},
        {"tollbooth_reset", "GRaceStatus::ResetRaceTimers", kResetRaceTimers, {0x51, 0x53, 0x56, 0x8B, 0xF1, 0x57, 0x33}, false, nfsmw::detour(&hook_reset_timers),
         nfsmw::orig(&g_orig_reset_timers)},
        {"tollbooth_message", "HUD_ShowTimeExtension", kShowTimeExtension, {0x6A, 0xFF, 0x68, 0x8F, 0x66, 0x87, 0x00}, false, nfsmw::detour(&hook_show_time_extension),
         nfsmw::orig(&g_orig_show_time_extension)},
        {"tollbooth_hud", "LocalPlayer::UpdateHud", kUpdateHud, {0x83, 0xEC, 0x38, 0x57, 0x8B, 0xF9, 0x8B}, false, nfsmw::detour(&hook_update_hud), nfsmw::orig(&g_orig_update_hud)},
        {"tollbooth_hud_message", "TimeExtension::Update", kTimeExtensionUpdate, {0xA1, 0x00, 0xE0, 0x91, 0x00, 0x83, 0xEC}, false, nfsmw::detour(&hook_time_extension_update),
         nfsmw::orig(&g_orig_time_extension_update)},
        {"tollbooth_hurry", "TimeExtension::SetPlayerLapTime", kSetPlayerLapTime, {0x8B, 0x44, 0x24, 0x04, 0x89, 0x41, 0x0C}, false, nfsmw::detour(&hook_set_lap_time),
         nfsmw::orig(&g_orig_set_lap_time)},
        {"tollbooth_is_player", "ISimable::IsPlayer", kIsPlayer, {0x8B, 0x01, 0xFF, 0x60, 0x24, 0xCC, 0xCC}, false, nfsmw::detour(&hook_is_player), nfsmw::orig(&g_orig_is_player)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), g_log, g_ini, "tollbooth clocks");
    if (!nfsmw::chain_call(kTickCall, reinterpret_cast<uintptr_t>(&tick))) g_log.line("tollbooth: the call at %#x is not a call: a player whose time is up is not knocked out", static_cast<unsigned>(kTickCall));
}

}  // namespace split_screen_support::tollbooth

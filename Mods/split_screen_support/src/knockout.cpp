/*
 * A player who is knocked out of a lap knockout race. `Game_KnockoutRacer` (0x611440) flags the racer (`GRacerInfo::KnockOut` 0x600210: +0x1C), tells the others
 * ("<name> knocked out", EKnockoutRacer) and stops the car with `IVehicle::Deactivate`, but only for an opponent: a player's car is left alone, and the race-over
 * message ("KNOCKED OUT": `RaceOverMessage::RequestRaceOverMessage` has a branch for the flag) is posted by the race's end for the player it ends the race for, which
 * in split screen is nobody (`Game_ShowRaceOverSummary` 0x6120C0 and `Game_NotifyRaceFinished` 0x6119F0 skip user mode 1). So a knocked out player kept driving, and was
 * only marked K.O. in the standings. The call of `KnockOut` is redirected: for a player's racer the car is stopped (`GRacerInfo::ForceStop` 0x5E9240: `IVehicle::ForceStopOn(1)`)
 * and the game's own message is posted for that player (`EShowRaceOverMessage` 0x625160, as `Game_ShowRaceOverSummary` makes it).
 * The race ends as in single player: `Game_AllHumanPlayersDone` (0x60E0A0, called by the race's script) counts a racer with the flag as done.
 */
#include "knockout.h"

#include "hooks.h"
#include "max_players.h"
#include "more_players.h"
#include "patch.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::knockout {

namespace {

constexpr uintptr_t kKnockOutCall = 0x61146A, kKnockOut = 0x600210, kForceStop = 0x5E9240, kGetRacerInfo = 0x5E8B20, kRaceStatus = 0x91E000, kEventNew = 0x627400,
                    kShowRaceOverMessage = 0x625160;
constexpr unsigned kKnockedOutFlag = 0x1C;  // GRacerInfo: knocked out
constexpr unsigned kPlayerGetSimable = 1;   // IPlayer virtual table slot

uintptr_t g_knock_out = kKnockOut;  // what the call goes to (another mod may have redirected it)

// The player (0-based) whose racer this is, -1 for an opponent.
int player_of(void *racer) {
    void *status = *reinterpret_cast<void **>(kRaceStatus);
    if (!status) return -1;
    for (int i = 0; i < nfsmw::kMaxPlayers; ++i) {
        void *player = more_players::game_player(i);
        if (!player) continue;
        void *simable = reinterpret_cast<void *(__fastcall *)(void *, void *)>((*reinterpret_cast<void ***>(player))[kPlayerGetSimable])(player, nullptr);
        if (simable && reinterpret_cast<void *(__fastcall *)(void *, void *, void *)>(kGetRacerInfo)(status, nullptr, simable) == racer) return i;
    }
    return -1;
}

void __fastcall hook_knock_out(void *racer, void *edx) {
    reinterpret_cast<void(__fastcall *)(void *, void *)>(g_knock_out)(racer, edx);
    if (!players::split_screen() || !*(static_cast<unsigned char *>(racer) + kKnockedOutFlag)) return;
    const int player = player_of(racer);
    if (player < 0) return;
    reinterpret_cast<void(__fastcall *)(void *, void *)>(kForceStop)(racer, nullptr);
    void *event = reinterpret_cast<void *(*)(unsigned)>(kEventNew)(0xC);
    if (event) reinterpret_cast<void *(__fastcall *)(void *, void *, void *)>(kShowRaceOverMessage)(event, nullptr, more_players::game_player(player));
    g_log.line("knockout: player %d is knocked out: the car is stopped%s", player + 1, event ? ", the message is posted" : ", NO message (no event)");
}

}  // namespace

void install() {
    if (const uintptr_t previous = nfsmw::chain_call(kKnockOutCall, reinterpret_cast<uintptr_t>(&hook_knock_out))) {
        g_knock_out = previous;
        g_log.line("knockout: a knocked out player's car will be stopped (the call went to %#x)", static_cast<unsigned>(previous));
    } else {
        g_log.line("knockout: the call at %#x is not a call; a knocked out player keeps driving", static_cast<unsigned>(kKnockOutCall));
    }
}

}  // namespace split_screen_support::knockout

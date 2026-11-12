/*
 * A player who is knocked out of a lap knockout race. `Game_KnockoutRacer` (0x611440) flags the racer (`GRacerInfo::KnockOut` 0x600210: +0x1C), tells the others
 * ("<name> knocked out", EKnockoutRacer) and stops the car with `IVehicle::Deactivate`, but only for an opponent: a player's car is left alone, and the race-over
 * message ("KNOCKED OUT": `RaceOverMessage::RequestRaceOverMessage` has a branch for the flag) is posted by the race's end for the player it ends the race for, which
 * in split screen is nobody (`Game_ShowRaceOverSummary` 0x6120C0 and `Game_NotifyRaceFinished` 0x6119F0 skip user mode 1). So a knocked out player kept driving, and was
 * only marked K.O. in the standings. The call of `KnockOut` is redirected: for a player's racer the car is stopped (`GRacerInfo::ForceStop` 0x5E9240: `IVehicle::ForceStopOn(1)`),
 * and the game's own message is posted for that player (`EShowRaceOverMessage` 0x625160, as `Game_ShowRaceOverSummary` makes it). Once the player's part of the screen
 * has gone to the others (layout.h) the car is taken out of the world as an opponent's is (`IVehicle::Deactivate`: a stopped car on the road is in the others' way); not
 * before, the player is still looking at it. A restart of the race clears the flag: the car is brought back then.
 * The race ends as in single player: `Game_AllHumanPlayersDone` (0x60E0A0, called by the race's script) counts a racer with the flag as done.
 */
#include "knockout.h"

#include "events.h"
#include "hooks.h"
#include "layout.h"
#include "max_players.h"
#include "more_players.h"
#include "patch.h"
#include "players.h"
#include "racers.h"
#include "split_screen_support.h"

namespace split_screen_support::knockout {

namespace {

constexpr uintptr_t kKnockOutCall = 0x61146A, kKnockOut = 0x600210, kForceStop = 0x5E9240, kEventNew = 0x627400,
                    kShowRaceOverMessage = 0x625160;

uintptr_t g_knock_out = kKnockOut;  // what the call goes to (another mod may have redirected it)
struct KnockedOut {
    unsigned char *racer;  // null: the player races
    ULONGLONG since;
    bool out_of_world;
} g_out[nfsmw::kMaxPlayers];

// The player (0-based) whose racer this is, -1 for an opponent.
int player_of(void *racer) {
    const int index = racers::index_of(static_cast<unsigned char *>(racer));
    for (int i = 0; i < nfsmw::kMaxPlayers && index >= 0; ++i)
        if (racers::of_player(more_players::game_player(i)) == index) return i;
    return -1;
}

void __fastcall hook_knock_out(void *racer, void *edx) {
    reinterpret_cast<void(__fastcall *)(void *, void *)>(g_knock_out)(racer, edx);
    if (!players::split_screen() || !static_cast<unsigned char *>(racer)[racers::kKnockedOut]) return;
    const int player = player_of(racer);
    if (player < 0) return;
    reinterpret_cast<void(__fastcall *)(void *, void *)>(kForceStop)(racer, nullptr);
    g_out[player] = {static_cast<unsigned char *>(racer), GetTickCount64(), false};
    void *event = reinterpret_cast<void *(*)(unsigned)>(kEventNew)(0xC);
    if (event) reinterpret_cast<void *(__fastcall *)(void *, void *, void *)>(kShowRaceOverMessage)(event, nullptr, more_players::game_player(player));
    g_log.line("knockout: player %d is knocked out: the car is stopped%s", player + 1, event ? ", the message is posted" : ", NO message (no event)");
}

void on_frame() {
    // Where the player keeps their part of the screen (Split Screen Overtake off), the car goes after the time the part would have taken to go.
    constexpr ULONGLONG kLatest = static_cast<ULONGLONG>((layout::kWaitSeconds + layout::kSlideSeconds) * 1000) + 500;
    for (int player = 0; player < nfsmw::kMaxPlayers; ++player) {
        KnockedOut &out = g_out[player];
        if (!out.racer) continue;
        if (racers::index_of(out.racer) < 0) {  // the race is over
            out.racer = nullptr;
        } else if (!out.racer[racers::kKnockedOut]) {  // the race was restarted
            if (out.out_of_world) {
                if (void *vehicle = racers::vehicle(racers::simable(out.racer))) racers::call_vehicle(vehicle, racers::kVehicleActivateSlot);
                g_log.line("knockout: the race started again: player %d's car is back in the world", player + 1);
            }
            out.racer = nullptr;
        } else if (!out.out_of_world && (layout::presence(player) <= 0.0f || GetTickCount64() - out.since >= kLatest)) {
            if (void *vehicle = racers::vehicle(racers::simable(out.racer))) racers::call_vehicle(vehicle, racers::kVehicleDeactivateSlot);
            out.out_of_world = true;
            g_log.line("knockout: player %d's car is out of the world", player + 1);
        }
    }
}

}  // namespace

void install() {
    if (const uintptr_t previous = nfsmw::chain_call(kKnockOutCall, reinterpret_cast<uintptr_t>(&hook_knock_out))) {
        g_knock_out = previous;
        events::on_frame(&on_frame);
        g_log.line("knockout: a knocked out player's car will be stopped (the call went to %#x)", static_cast<unsigned>(previous));
    } else {
        g_log.line("knockout: the call at %#x is not a call; a knocked out player keeps driving", static_cast<unsigned>(kKnockOutCall));
    }
}

}  // namespace split_screen_support::knockout

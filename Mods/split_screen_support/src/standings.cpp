/*
 * Who is who in the race's standings, for more than two players and more than four racers.
 *
 *  names        GRaceStatus::AddSimablePlayer 0x5E8BF0 names a player's racer (GRacerInfo +8, a pointer kept for the race) in split screen (Sim::GetUserMode() == 1)
 *               "Player 1" when it is the first racer and "Player 2" otherwise, so players 3 and 4 were "Player 2" in the standings, the HUD's leaderboard and the
 *               "<name> knocked out" message (EKnockoutRacer). Each player's racer is named after the player whose car it is.
 *  leaderboard  LocalPlayer::UpdateHud 0x6F13F0 fills the HUD's leaderboard with the racers ranked 1 to 4 and tells it the row of the HUD's own racer
 *               (ILeaderBoard::SetPlayerIndex) only when it finds that racer among them: the game had four racers at most. Ranked lower, the HUD kept the
 *               row it had last, so another racer's lap was shown as the player's lap time (and split times and the distances on the board were measured
 *               from that racer). A player ranked below 4 now gets the fourth row, with their own rank.
 */
#include "standings.h"

#include "hooks.h"
#include "max_players.h"
#include "more_players.h"
#include "patch.h"
#include "split_screen_support.h"

namespace split_screen_support::standings {

namespace {

constexpr uintptr_t kAddSimablePlayer = 0x5E8BF0, kGetUserMode = 0x6E8DF0;
constexpr int kRacerName = 8;  // GRacerInfo: const char *

char g_names[nfsmw::kMaxPlayers][32];

void *(__fastcall *g_orig_add_player)(void *, void *, void *);
void *__fastcall hook_add_player(void *status, void *edx, void *simable) {
    void *info = g_orig_add_player(status, edx, simable);
    if (!info || !simable || reinterpret_cast<int (*)()>(kGetUserMode)() != 1) return info;
    void **vtable = *reinterpret_cast<void ***>(simable);
    void *player = reinterpret_cast<void *(__fastcall *)(void *, void *)>(vtable[0x20 / 4])(simable, nullptr);  // ISimable::GetPlayer
    if (!player) return info;
    // From the last: while the extra players' cars are made, the local player list holds the extra players.
    for (int i = nfsmw::kMaxPlayers; i-- > 0;) {
        if (more_players::game_player(i) != player) continue;
        more_players::player_label(i, g_names[i], sizeof(g_names[i]));
        *reinterpret_cast<const char **>(static_cast<char *>(info) + kRacerName) = g_names[i];
        g_log.line("standings: player %d's racer is \"%s\"", i + 1, g_names[i]);
        break;
    }
    return info;
}

// In UpdateHud's loop over the leaderboard's rows (ebp the row, [esp+0x2C] the index of the racer ranked row + 1 or -1, [esp+0x3C] the HUD's own racer's
// GRacerInfo or null), before the row is filled: the fourth row takes the HUD's own racer when it is ranked lower (GRacerInfo +0x10 rank, +0xC index).
constexpr uintptr_t kFillRow = 0x6F1F25;
void install_leaderboard() {
    static nfsmw::Stub stub;
    stub.emit({0x83, 0xFD, 0x03});  // cmp ebp, 3
    const size_t not_last = stub.jump_short(0x75);
    stub.emit({0x8B, 0x44, 0x24, 0x3C});  // mov eax, [esp+0x3C]
    stub.emit({0x85, 0xC0});  // test eax, eax
    const size_t no_racer = stub.jump_short(0x74);
    stub.emit({0x83, 0x78, 0x10, 0x04});  // cmp dword ptr [eax+0x10], 4
    const size_t on_board = stub.jump_short(0x7E);
    stub.emit({0x8B, 0x40, 0x0C});  // mov eax, [eax+0xC]
    stub.emit({0x89, 0x44, 0x24, 0x2C});  // mov [esp+0x2C], eax
    stub.resolve_short(not_last);
    stub.resolve_short(no_racer);
    stub.resolve_short(on_board);
    const unsigned char expected[] = {0x8B, 0x16, 0x6A, 0x00, 0x55};  // mov edx, [esi]; push 0; push ebp
    stub.emit({0x8B, 0x16, 0x6A, 0x00, 0x55});
    stub.emit_rel32(0xE9, kFillRow + sizeof(expected));
    stub.done();
    const bool done = nfsmw::jump_to_stub(kFillRow, expected, sizeof(expected), stub);
    g_log.line("standings: a player ranked below 4 %s", done ? "has the fourth row of their leaderboard" : "NOT patched (the code differs)");
}

}  // namespace

void install() {
    const nfsmw::Hook hooks[] = {
        {"racernames", "GRaceStatus::AddSimablePlayer", kAddSimablePlayer, {0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x58}, false, nfsmw::detour(&hook_add_player),
         nfsmw::orig(&g_orig_add_player)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "standings");
    install_leaderboard();
}

}  // namespace split_screen_support::standings

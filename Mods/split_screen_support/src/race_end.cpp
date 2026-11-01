/*
 * The end of a split screen race: the results screen came the frame the last player finished. In a race of one player the game shows the race-over message ("1ST", a time, or
 * "KNOCKED OUT") and the results come a few seconds later; here every call of `FE_ShowPostRaceScreen` (0x60AC20: Game_EnterPostRaceFlow, and the Lua bindings
 * FE_ShowWinning / LosingPostRaceScreen) is held back until 2 seconds have passed since the last player got their message (the grace the game has when opponents knock out the
 * last players, seen to be about 2 seconds). A player who has no message by then gets one (`EShowRaceOverMessage` 0x625160 is what makes it: the finish of a
 * racer in Game_NotifyRaceFinished, knockout.cpp, and here; hooked to know who has had theirs). The race goes on behind the message; the results open from the frame hook.
 */
#include "race_end.h"

#include <windows.h>

#include "events.h"
#include "hooks.h"
#include "max_players.h"
#include "more_players.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::race_end {

namespace {

constexpr uintptr_t kShowPostRaceScreen = 0x60AC20, kShowRaceOverMessage = 0x625160, kEventNew = 0x627400, kGameFlowState = 0x925E90;
constexpr int kInRace = 6;  // the game flow state
constexpr int kDelayMs = 2000;

bool g_shown[nfsmw::kMaxPlayers];  // the player has had the race-over message of this race
bool g_have_message = false;
DWORD g_last_message = 0;
bool g_pending = false, g_passing = false;  // the results are waiting; the held call is being made
DWORD g_due = 0;
char g_arg = 1;

void *(__fastcall *g_orig_message)(void *, void *, void *);
void (*g_orig_show)(char);

int player_index(void *player) {
    for (int i = 0; i < nfsmw::kMaxPlayers; ++i)
        if (player && more_players::game_player(i) == player) return i;
    return -1;
}

void *__fastcall hook_message(void *event, void *edx, void *player) {
    const int index = player_index(player);
    if (index >= 0) g_shown[index] = true;
    g_have_message = true;
    g_last_message = GetTickCount();
    return g_orig_message(event, edx, player);
}

void post_message(void *player) {
    if (void *event = reinterpret_cast<void *(*)(unsigned)>(kEventNew)(0xC)) reinterpret_cast<void *(__fastcall *)(void *, void *, void *)>(kShowRaceOverMessage)(event, nullptr, player);
}

void hook_show(char arg) {
    if (g_passing || !players::split_screen()) return g_orig_show(arg);
    for (int i = 0; i < nfsmw::kMaxPlayers; ++i) {
        void *player = more_players::game_player(i);
        if (player && !g_shown[i]) post_message(player);
    }
    if (!g_have_message) {  // (no player to tell: count from now)
        g_have_message = true;
        g_last_message = GetTickCount();
    }
    g_due = g_last_message + kDelayMs;
    if (static_cast<int>(GetTickCount() - g_due) >= 0) return g_orig_show(arg);
    g_pending = true;
    g_arg = arg;
    g_log.line("race end: the results screen opens in %d ms", static_cast<int>(g_due - GetTickCount()));
}

void on_frame() {
    if (!g_pending) return;
    if (*reinterpret_cast<int *>(kGameFlowState) != kInRace || !players::split_screen()) {  // (the race was left some other way)
        g_pending = false;
        return;
    }
    if (static_cast<int>(GetTickCount() - g_due) < 0) return;
    g_pending = false;
    g_passing = true;
    g_orig_show(g_arg);
    g_passing = false;
}

}  // namespace

void race_started() {
    for (bool &shown : g_shown) shown = false;
    g_have_message = false;
    g_pending = false;
}

void install() {
    const nfsmw::Hook hooks[] = {
        {"racemessage", "EShowRaceOverMessage::EShowRaceOverMessage", kShowRaceOverMessage, {0x8B, 0xC1, 0x8B, 0x4C, 0x24, 0x04, 0xC7}, false, nfsmw::detour(&hook_message),
         nfsmw::orig(&g_orig_message)},
        {"postracescreen", "FE_ShowPostRaceScreen", kShowPostRaceScreen, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B}, false, nfsmw::detour(&hook_show), nfsmw::orig(&g_orig_show)},
    };
    const auto result = nfsmw::install_hooks(hooks, 2, g_log, g_ini, "race end");
    if (result.created == 2) events::on_frame(on_frame);
    g_log.line("race end: the results screen waits %d ms after the last race-over message%s", kDelayMs, result.created == 2 ? "" : " (NOT installed: the code differs)");
}

}  // namespace split_screen_support::race_end

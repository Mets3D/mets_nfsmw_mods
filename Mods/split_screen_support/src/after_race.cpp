/*
 * A race ends, or is quit from the pause menu, with an EQuitToFE event (constructor 0x62FE40: garage type +8, screen name +0xC). Its destructor (0x623F80) is where the
 * game decides what the front end shows first: it sets FEManager +0xC, the package to push once the front end is loaded, which for a quick race is MainMenu_Sub.fng: the
 * Quick Race menu, from which a split screen game takes five screens of clicking to start again. After a split screen race this puts the race's own options screen there
 * (Track_Options.fng: the track, laps and the Players row stay as they were; Back goes to the track choice), or the track choice, as `[options] after_race` says.
 */
#include "after_race.h"

#include <cstring>

#include "hooks.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::after_race {

namespace {

constexpr uintptr_t kFEManagerGet = 0x516E10;
constexpr unsigned kFirstScreen = 0xC;  // FEManager: the package the front end shows first
const char *g_screen = "Track_Options.fng";

void(__fastcall *g_orig_quit_to_fe_end)(void *);
void __fastcall hook_quit_to_fe_end(void *self) {
    const bool split = players::split_screen();
    g_orig_quit_to_fe_end(self);
    if (!split || !g_screen) return;
    unsigned char *manager = reinterpret_cast<unsigned char *(*)()>(kFEManagerGet)();
    const char *&first = *reinterpret_cast<const char **>(manager + kFirstScreen);
    if (!first || strcmp(first, "MainMenu_Sub.fng") != 0) return;  // (a career race, a quit to the main menu: not ours to change)
    first = g_screen;
    g_log.line("after the split screen race the front end starts at %s", g_screen);
}

}  // namespace

void install() {
    char choice[32];
    GetPrivateProfileStringA("options", "after_race", "track_options", choice, sizeof(choice), g_ini);
    if (!_stricmp(choice, "track_select")) g_screen = "Track_Select.fng";
    else if (!_stricmp(choice, "menu")) g_screen = nullptr;  // the game's own: the Quick Race menu
    if (!g_screen) return;
    const nfsmw::Hook hooks[] = {
        {"quittofe", "EQuitToFE::~EQuitToFE", 0x623F80, {0x6A, 0xFF, 0x68, 0xB8, 0x72, 0x87, 0x00}, false, nfsmw::detour(&hook_quit_to_fe_end),
         nfsmw::orig(&g_orig_quit_to_fe_end)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "after race");
}

}  // namespace split_screen_support::after_race

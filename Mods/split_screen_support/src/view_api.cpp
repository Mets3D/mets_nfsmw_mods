#include "view_api.h"

#include "more_players.h"
#include "hud_players.h"
#include "players.h"
#include "split_screen_api.h"

namespace split_screen_support::view_api {

namespace {

nfsmw::split_screen_api::ViewDrawFn g_callbacks[8];
volatile int g_count = 0;

}  // namespace

void draw(IDirect3DDevice9 *device, const void *view_plat_info, float width, float height, int player) {
    const int count = g_count;
    for (int i = 0; i < count; ++i) g_callbacks[i](device, view_plat_info, width, height, player);
}

bool wanted() { return g_count > 0; }

}  // namespace split_screen_support::view_api

// For automated tests (nfsmw_dev_tools): the Press Start screen is told that a device (0-3 controller, -1 keyboard) let go of Start.
extern "C" __declspec(dllexport) void nfsmw_split_screen_support_press_start(int device) {
    split_screen_support::players::tell_press_start(device);
}

// Whose Player options are open (0 to 3): the player who asked for the pause menu, or -1 when that is not known (the main menu, a game of one player).
extern "C" __declspec(dllexport) int nfsmw_split_screen_support_options_player() { return split_screen_support::hud_players::pause_requester(); }

extern "C" __declspec(dllexport) void nfsmw_split_screen_support_log_players() { split_screen_support::more_players::log_players(); }

extern "C" __declspec(dllexport) void nfsmw_split_screen_support_on_view_draw(nfsmw::split_screen_api::ViewDrawFn callback) {
    using namespace split_screen_support::view_api;
    if (callback && g_count < 8) {
        g_callbacks[g_count] = callback;
        g_count = g_count + 1;
    }
}

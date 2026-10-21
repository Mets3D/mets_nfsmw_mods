/*
 * nfsmw_improved_ai_opponents: the game's racing AI for the players' cars (the "Drunk Driver" of Extra Options, for each player), an improved version of it,
 * and drawings of what the AI does in the 3D world. It needs nfsmw_split_screen_support for now: that mod gives it a place in every frame and a
 * hook into each player's picture (common/split_screen_api.h).
 *   ai_control.cpp  who is AI driven (per player, the ini and F7), the AI's nitrous and stuck-timer changes, F9: plan the route again
 *   path_cost.cpp   the improved AI's route search weighs road width and what the car can do, and may plan shortcuts
 *   overlay.cpp     lines in the picture showing what the AI does (the route, the corridor, the steering target) and the road network (F8)
 * What is known about the game's code these parts use: agent_docs/ai-driving.md.
 *
 * Files in <game>/scripts/mets_mods/: nfsmw_improved_ai_opponents.log, nfsmw_improved_ai_opponents.ini.
 */
#include "improved_ai_opponents.h"

#include "ai_control.h"
#include "overlay.h"
#include "path_cost.h"
#include "settings.h"
#include "split_screen_api.h"

NFSMW_PLUGIN_DECLARE("NFSMW Improved AI Opponents", "1.0.0", "Demeter Dzadik")

namespace improved_ai_opponents {

nfsmw::Logger g_log;
char g_ini[MAX_PATH];
bool g_active = false;
bool g_debug_log = false;

namespace {

// Once per player's picture (see split_screen_api.h); the first one of a frame is also the frame's tick.
void on_view_draw(IDirect3DDevice9 *device, const void *view_plat_info, float width, float height, int player) {
    if (player == 0) control::frame();
    overlay::draw(device, view_plat_info, width, height, player);
}

}  // namespace

}  // namespace improved_ai_opponents

NFSMW_PLUGIN_MAIN() {
    using namespace improved_ai_opponents;
    g_log.open_in_data_folder("nfsmw_improved_ai_opponents.log");
    g_log.line("nfsmw_improved_ai_opponents 1.0.0 starting");
    nfsmw::data_file("nfsmw_improved_ai_opponents.ini", g_ini, sizeof(g_ini));
    g_debug_log = ini_flag("debug", "log", false);
    const auto on_view_draw_register = nfsmw::split_screen_api::find_on_view_draw();
    if (!on_view_draw_register) {
        g_log.line("nfsmw_split_screen_support is not loaded: it gives this mod its place in every frame, so nothing is changed");
        return NFSMW_OK;
    }
    settings::load();
    control::install();
    path_cost::install();
    overlay::install();
    on_view_draw_register(&on_view_draw);
    g_active = true;
    return NFSMW_OK;
}

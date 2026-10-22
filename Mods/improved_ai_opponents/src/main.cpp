/*
 * nfsmw_improved_ai_opponents: the game's racing AI for the players' cars (the "Drunk Driver" of Extra Options, for each player), an improved version of it,
 * and drawings of what the AI does in the 3D world. It needs the nfsmw_split_screen_support plugin for now (not a split screen race): that mod gives it a place
 * in every frame and a hook into each player's picture, or into the one picture of a game that is not split (common/split_screen_api.h).
 *   ai_control.cpp  who is AI driven (per player, the ini and F7), the AI's nitrous, stuck-timer, skill and steering changes, F9: plan the route again
 *   car_stats.cpp   what a player's car can do (top speed, acceleration, grip), from the game
 *   path_cost.cpp   the improved AI's route search costs the time a route takes for that car, and may plan shortcuts
 *   nav_landing.cpp, race_area.cpp   where the AI's nav may be put, and the area of the race
 *   road_geometry.cpp   the curves of the road segments and how fast a car can follow them
 *   overlay.cpp     lines in the picture showing what the AI does (the route, the steering target) (F8 steps through the overlays); lines.cpp and segment_draw.cpp draw them
 *   road_inspector.cpp the road network around the camera, node and segment by segment; the cost of a route picked node by node (F8 modes Roads and Cost)
 * What is known about the game's code these parts use: agent_docs/ai-driving.md.
 *
 * Files in <game>/scripts/mets_mods/: nfsmw_improved_ai_opponents.log, nfsmw_improved_ai_opponents.ini.
 */
#include "improved_ai_opponents.h"

#include "ai_control.h"
#include "car_stats.h"
#include "overlay.h"
#include "nav_landing.h"
#include "obstacles.h"
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
    obstacles::install();
    path_cost::install();
    nav_landing::install();
    on_view_draw_register(&on_view_draw);
    g_active = true;
    return NFSMW_OK;
}

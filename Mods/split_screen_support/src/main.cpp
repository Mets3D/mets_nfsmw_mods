/*
 * nfsmw_split_screen_support: the PC version of the game has much of the console versions' two player split screen
 * ("Split Screen" in the Quick Race menu) but not the way in, a renderer for two views or an input device for a second player
 * (that comes from mets_controller_support).
 * Each part has its own file, header and namespace. Split screen needs all of them but the minimap, which has a switch in
 * nfsmw_split_screen_support.ini ([options] use_split_minimap), as has the scenery detail rule in fixes.cpp ([options] cull_small_objects):
 *   menu_entry.cpp     the "Split Screen" entry of the Quick Race menu
 *   fixes.cpp          the changes that let the race load and show
 *   split_view.cpp     the screen in two (or four): the world drawn once per player, each player's shadow map
 *   reflections.cpp    the car reflection cube map of each player (a copy of its six faces per player)
 *   layout.h           where each player's picture goes (a grid of cells)
 *   hud_views.cpp      each player's HUD package drawn into their cell of the screen
 *   hud_players.cpp    a HUD with a minimap for each of four players (copies of HUD_Player1.fng made by hud_packages.cpp)
 *   minimap.cpp        a minimap for each player, a colour for each player's marker
 *   minimap_option.cpp the Options > Player > Minimap row: each player can hide their own minimap
 *   player_count.cpp   the "Players" row (2 or 4) of the split screen race options
 *   more_players.cpp   players 3 and 4: a second round of Press Start and Car Select, their picks kept in a table, their cars in the race
 *   extra_streaming.cpp the track streamer's two positions chosen among four cars
 *   players.cpp        which controller is whose (XInput), player 1's alone reaches the game
 *   state_log.cpp      a log of the player count, devices and view mode
 *   view_api.cpp       what other mods may draw into each player's picture (common/split_screen_api.h), e.g. nfsmw_improved_ai_opponents's overlay
 *   events.cpp         the game functions several parts need (world load, each frame)
 * What is known about the game's code these parts change: agent_docs/split-screen.md.
 *
 * Files in <game>/scripts/mets_mods/: nfsmw_split_screen_support.log, nfsmw_split_screen_support.ini (optional).
 */
#include "split_screen_support.h"

#include "after_race.h"
#include "ai_in_split_screen.h"
#include "engine_sounds.h"
#include "events.h"
#include "extra_cameras.h"
#include "extra_streaming.h"
#include "fixes.h"
#include "hud_players.h"
#include "hud_views.h"
#include "menu_entry.h"
#include "minimap.h"
#include "minimap_option.h"
#include "more_players.h"
#include "player_count.h"
#include "player_settings.h"
#include "players.h"
#include "race_intro.h"
#include "skin_slots.h"
#include "split_view.h"
#include "streaming_log.h"
#include "streaming_pool.h"
#include "start_grid.h"
#include "state_log.h"

NFSMW_PLUGIN_DECLARE("NFSMW Split Screen Support", "1.0.0", "Demeter Dzadik")

namespace split_screen_support {

nfsmw::Logger g_log;
char g_ini[MAX_PATH];

}  // namespace split_screen_support

NFSMW_PLUGIN_MAIN() {
    using namespace split_screen_support;
    g_log.open_in_data_folder("nfsmw_split_screen_support.log");
    g_log.line("nfsmw_split_screen_support 1.0.0 starting");
    nfsmw::data_file("nfsmw_split_screen_support.ini", g_ini, sizeof(g_ini));
    menu_entry::install();
    fixes::install();
    split_view::install();
    after_race::install();
    minimap::install();
    hud_views::install();
    hud_players::install();
    minimap_option::install();
    if (ini_flag("options", "more_players", false)) {  // work in progress: see the ini
        player_count::install();
        more_players::install();
        extra_streaming::install();
        extra_cameras::install();
        start_grid::install();
        player_settings::install();
    }
    race_intro::install();
    streaming_log::install();
    skin_slots::install();
    engine_sounds::install();
    if (ini_flag("options", "ai_in_split_screen", true)) ai_in_split_screen::install();  // after player_count: its rows come after the Players row
    more_players::install_car_select_label();
    players::install();  // before state_log: the world load log shows its ports
    state_log::install();
    events::install();
    return NFSMW_OK;
}

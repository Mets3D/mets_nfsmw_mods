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
 *   layout.h/.cpp      where each player's picture goes (a grid of cells; the cells of players who are out slide away)
 *   hud_views.cpp      each player's HUD package drawn into their cell of the screen
 *   hud_players.cpp    a HUD with a minimap for each of four players (copies of HUD_Player1.fng made by hud_packages.cpp)
 *   minimap.cpp        a minimap for each player, a colour for each player's marker
 *   minimap_option.cpp the Options > Player > Minimap row: each player can hide their own minimap
 *   player_count.cpp   the "Players" row (2 or 4) of the split screen race options
 *   race_rows.cpp      its Layout, Start Lineup and (tollbooth) Time rows
 *   more_players.cpp   players 3 and 4: a second round of Press Start and Car Select, their picks kept in a table, their cars in the race
 *   standings.cpp      the racers' names of players 3 and 4, the leaderboard of a player ranked below 4
 *   hud_copies.cpp    the leaderboard and the mirror's housing of a split screen HUD (copies of the single player HUD package's)
 *   mirrors.cpp        a rear view mirror for each player
 *   ea_trax.cpp        hide the EA Trax song display, which covers player 1's picture
 *   knockout.cpp       a player knocked out of a lap knockout: the car stops and leaves the world, the game's message shows
 *   tollbooth.cpp      a clock for each racer of a tollbooth race; a racer whose time is up is knocked out
 *   tollbooth_results.cpp the end of a tollbooth race: the finish message, the order of the racers and the results screen
 *   race_end.cpp       the end of a race: every player gets the race-over message, the results wait a few seconds
 *   world_map.cpp      the world map shows every player and the opponents, in the colours of the minimaps
 *   extra_streaming.cpp the track streamer's two positions chosen among four cars
 *   players.cpp        which device is whose (the Press Start screens)
 *   player_devices.cpp a game input device for each player, that says what the player's device alone asks for
 *   keyboard2.cpp      Keyboard 2: a second player on the same keyboard, with keys of their own
 *   state_log.cpp      a log of the player count, devices and view mode
 *   view_api.cpp       what other mods may draw into each player's picture (common/split_screen_api.h), e.g. nfsmw_improved_ai_opponents's overlay
 *   events.cpp         the game functions several parts need (world load, each frame)
 * What is known about the game's code these parts change: agent_docs/split-screen.md.
 *
 * Files in <game>/scripts/mets_mods/: nfsmw_split_screen_support.log, nfsmw_split_screen_support.ini (optional).
 */
#include "split_screen_support.h"

#include "custom_menus_api.h"
#include "after_race.h"
#include "ai_in_split_screen.h"
#include "engine_sounds.h"
#include "ea_trax.h"
#include "split_volume.h"
#include "knockout.h"
#include "race_end.h"
#include "tollbooth.h"
#include "tollbooth_results.h"
#include "standings.h"
#include "world_map.h"
#include "events.h"
#include "extra_cameras.h"
#include "extra_streaming.h"
#include "fixes.h"
#include "hud_copies.h"
#include "hud_players.h"
#include "hud_views.h"
#include "keyboard2.h"
#include "layout.h"
#include "menu_entry.h"
#include "minimap.h"
#include "minimap_option.h"
#include "race_again.h"
#include "more_players.h"
#include "player_count.h"
#include "mirrors.h"
#include "race_rows.h"
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

// Called by nfsmw_custom_menus when the Options menu first opens.
extern "C" __declspec(dllexport) void nfsmw_custom_menus_provide(const nfsmw::custom_menus::Host *host) {
    char translations[MAX_PATH];
    nfsmw::data_file("translations\\split_screen_support", translations, sizeof(translations));
    host->register_translations(translations);  // before any text is registered
    split_screen_support::minimap_option::register_page(host);
    split_screen_support::mirrors::register_page(host);
    split_screen_support::layout::register_page(host);
    split_screen_support::player_count::register_texts(host);
    split_screen_support::race_rows::register_texts(host);
    split_screen_support::ai_in_split_screen::register_texts(host);
}

NFSMW_PLUGIN_MAIN() {
    using namespace split_screen_support;
    g_log.open_in_data_folder("nfsmw_split_screen_support.log");
    g_log.line("nfsmw_split_screen_support 1.0.0 starting");
    nfsmw::data_file("nfsmw_split_screen_support.ini", g_ini, sizeof(g_ini));
    menu_entry::install();
    fixes::install();
    split_view::install();
    layout::install();
    after_race::install();
    minimap::install();
    hud_views::install();
    hud_players::install();
    hud_copies::install();
    minimap_option::install();
    player_count::install();
    race_rows::install();  // (its rows come under the Players row)
    more_players::install();
    extra_streaming::install();
    extra_cameras::install();
    mirrors::install();
    start_grid::install();
    player_settings::install();
    race_again::install();  // (the picks it races with are kept by more_players)
    race_intro::install();
    streaming_log::install();
    skin_slots::install();
    engine_sounds::install();
    if (ini_flag("options", "hide_ea_trax", true)) ea_trax::install();
    split_volume::install();
    standings::install();
    knockout::install();
    tollbooth::install();
    tollbooth_results::install();
    race_end::install();
    world_map::install();
    if (ini_flag("options", "enable_ai_opponents_in_split_screen", true)) ai_in_split_screen::install();  // after player_count: its rows come after the Players row
    more_players::install_car_select_label();
    more_players::install_shared_stable();
    players::install();  // before state_log: the world load log shows its ports
    if (ini_flag("options", "simulate_two_keyboards", true)) keyboard2::install();
    state_log::install();
    events::install();
    return NFSMW_OK;
}

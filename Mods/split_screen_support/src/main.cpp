/*
 * nfsmw_split_screen_support: the PC version of the game has much of the console versions' two player split screen
 * ("Split Screen" in the Quick Race menu) but not the way in, a renderer for two views or an input device for a second player
 * (that comes from mets_controller_support).
 * Each part has its own file, header and namespace. Split screen needs all of them but the minimap, which has a switch in
 * nfsmw_split_screen_support.ini ([options] use_split_minimap), as has the scenery detail rule in fixes.cpp ([options] cull_small_objects):
 *   menu_entry.cpp     the "Split Screen" entry of the Quick Race menu
 *   fixes.cpp          the changes that let the race load and show
 *   split_view.cpp     the screen in two: the world drawn once per player, each player's shadow map
 *   minimap.cpp        a minimap for each player, a colour for each player's marker
 *   minimap_option.cpp the Options > Player > Minimap row: each player can hide their own minimap
 *   players.cpp        which controller is whose (XInput), player 1's alone reaches the game
 *   state_log.cpp      a log of the player count, devices and view mode
 *   view_api.cpp       what other mods may draw into each player's picture (common/split_screen_api.h), e.g. nfsmw_improved_ai_opponents's overlay
 *   events.cpp         the game functions several parts need (world load, each frame)
 * What is known about the game's code these parts change: agent_docs/split-screen.md.
 *
 * Files in <game>/scripts/mets_mods/: nfsmw_split_screen_support.log, nfsmw_split_screen_support.ini (optional).
 */
#include "split_screen_support.h"

#include "events.h"
#include "fixes.h"
#include "menu_entry.h"
#include "minimap.h"
#include "minimap_option.h"
#include "players.h"
#include "split_view.h"
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
    if (ini_flag("options", "use_split_minimap")) minimap::install();
    minimap_option::install();
    players::install();  // before state_log: the world load log shows its ports
    state_log::install();
    events::install();
    return NFSMW_OK;
}

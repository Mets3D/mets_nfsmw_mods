/*
 * nfsmw_split_screen_support: the PC version of the game has much of the console versions' two player split screen
 * ("Split Screen" in the Quick Race menu) but not the way in, a renderer for two views or input for a second player.
 * Each part has its own file, header and namespace, and its own switch in nfsmw_split_screen_support.ini:
 *   menu_entry.cpp     the "Split Screen" entry of the Quick Race menu                      [menu] entry
 *   fixes.cpp          the changes that let the race load and show                           [fixes] shared_device, loading_screen, view_mode
 *   split_view.cpp     the screen in two: the world drawn once per player                     [render] split, layout, band_center
 *   streaming.cpp      the world loaded around player 2 too                                  [streaming] player2
 *   players.cpp        which controller is whose (XInput), player 1's alone reaches the game  [players] enabled
 *   player2_input.cpp  player 2's car driven from player 2's controller                       [player2] enabled, buttons
 *   state_log.cpp      a log of the player count, devices and view mode                       [log] state
 *   events.cpp         the game functions several parts need (world load, each frame)
 *
 * Files in <game>/scripts/mets_mods/: nfsmw_split_screen_support.log, nfsmw_split_screen_support.ini (optional).
 */
#include "split_screen_support.h"

#include "events.h"
#include "fixes.h"
#include "menu_entry.h"
#include "player2_input.h"
#include "players.h"
#include "split_view.h"
#include "state_log.h"
#include "streaming.h"

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
    if (ini_flag("menu", "entry")) menu_entry::install();
    fixes::install();
    if (ini_flag("render", "split")) split_view::install();
    if (ini_flag("streaming", "player2")) streaming::install();
    if (ini_flag("players", "enabled")) players::install();  // before state_log: the world load log shows its ports
    if (ini_flag("player2", "enabled")) player2_input::install();
    if (ini_flag("log", "state")) state_log::install();
    events::install();
    return NFSMW_OK;
}

/*
 * nfsmw_controller_support: controller input and rumble for speed.exe 1.3 (see README.md). Entry point: the parts are in their own
 * files, each with its own header and namespace:
 *   settings.cpp       nfsmw_controller_support.ini and the log
 *   input.cpp          DirectInput state (dead zones, separate triggers, mouse) and the analog action mapping
 *   bindings.cpp       the PC binding table: our entries (camera look), a log of binding changes
 *   pad_sets.cpp       one set of bindings per controller, and the Controls menu's list of them
 *   controls_menu.cpp  Options > Controls: readable names for XInput bindings
 *   default_layout.cpp our default controller bindings (RT accelerates, ...)
 *   look.cpp           camera look
 *   button_icons.cpp   controller pictures in the button prompts of the menus, instead of keyboard keys
 *   prompts_option.cpp Options > Player > Controller Prompts: which controller's buttons the prompts show
 *   menu_buttons.cpp   A accepts, B goes back in the menus; X, View and Menu press the hotkeys 2, 3, 4; B quits from the main menu
 *   world_map.cpp      the world map: the right stick moves its cursor, LT and RT zoom
 *   extra_device.cpp   a second game input device, so that a second local player has an input path of the game's own
 *   rumble.cpp         rumble (the force feedback events, the controller's motors)
 *   rumble_page.cpp    Options > Rumble, a page of nfsmw_custom_menus (no entry in the Options menu)
 *   controls_rumble.cpp the Controls screen's Rumble hint opens that page for the selected controller, and comes back
 *   menu_pages.cpp     registers the pages of this mod with nfsmw_custom_menus
 */
#include "bindings.h"
#include "button_icons.h"
#include "controls_menu.h"
#include "default_layout.h"
#include "pad_sets.h"
#include "input.h"
#include "look.h"
#include "menu_buttons.h"
#include "pad.h"
#include "rumble.h"
#include "world_map.h"

NFSMW_PLUGIN_DECLARE("NFSMW Pad", "1.0.0", "Demeter")

NFSMW_PLUGIN_MAIN() {
    using namespace pad;
    g_log.open_in_data_folder("nfsmw_controller_support.log");
    log_line("nfsmw_controller_support 1.0.0 starting");
    load_settings();
    if (!g_cfg.enabled) return NFSMW_OK;
    xinput::load(&g_log);
    pad_sets::load_settings();
    bindings::prepare_default_look_entries();
    menu_buttons::patch_default_table();
    default_layout::patch_default_table();
    input::install_hooks();
    bindings::install_hooks();
    pad_sets::install_hooks();
    look::install_hooks();
    button_icons::install_hooks();
    rumble::install_hooks();
    world_map::install_hooks();
    rumble::start();
    return NFSMW_OK;
}

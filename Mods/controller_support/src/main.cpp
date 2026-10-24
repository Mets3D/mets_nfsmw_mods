/*
 * mets_controller_support: controller input and rumble for speed.exe 1.3 (see README.md). Entry point: the parts are in their own
 * files, each with its own header and namespace:
 *   settings.cpp       mets_controller_support.ini and the log
 *   input.cpp          DirectInput state (dead zones, separate triggers, mouse) and the analog action mapping
 *   bindings.cpp       the PC binding table: our entries (camera look), a log of binding changes
 *   pad_sets.cpp       one set of bindings per controller, and the Controls menu's list of them
 *   controls_menu.cpp  Options > Controls: readable names for XInput bindings
 *   default_layout.cpp our default controller bindings (RT accelerates, ...)
 *   look.cpp           camera look
 *   button_icons.cpp   controller pictures in the button prompts of the menus, instead of keyboard keys
 *   menu_buttons.cpp   A accepts, B goes back in the menus; X, View and Menu press the hotkeys 2, 3, 4; B quits from the main menu
 *   world_map.cpp      the world map: the right stick moves its cursor, LT and RT zoom
 *   showroom.cpp       the showroom (the garage's car view): LT and RT zoom by how far they are pulled, LB and RB fully
 *   new_controllers.cpp controllers connected after the game started are handed to it like the first ones
 *   extra_device.cpp   a second game input device, so that a second local player has an input path of the game's own
 *   rumble.cpp         rumble (the force feedback events, the controller's motors)
 *   device_settings.cpp  the dead zones of each XInput controller
 *   controller_page.cpp  the Settings page (prompts, dead zones, rumble), a page of nfsmw_custom_menus (no entry in the Options menu)
 *   controls_options.cpp the Controls screen's hint opens that page for the selected controller, and comes back
 *   menu_pages.cpp     registers the pages of this mod with nfsmw_custom_menus
 */
#include "early_entry.h"
#include "bindings.h"
#include "button_icons.h"
#include "controls_menu.h"
#include "default_layout.h"
#include "pad_sets.h"
#include "input.h"
#include "look.h"
#include "menu_buttons.h"
#include "new_controllers.h"
#include "pad.h"
#include "rumble.h"
#include "showroom.h"
#include "widescreen_ini.h"
#include "world_map.h"

NFSMW_PLUGIN_DECLARE("NFSMW Pad", "1.0.0", "Demeter")

namespace {
nfsmw::widescreen_ini::Change g_widescreen_change = nfsmw::widescreen_ini::Change::kNone;
char g_widescreen_ini[MAX_PATH];
}  // namespace

// The widescreen fix reads its ini as soon as its .asi is loaded, so this has to run when ours is loaded, which is before it:
// the loader loads a folder's plugins alphabetically, and "mets_controller_support" comes before "NFSMostWanted...".
// Its ImproveGamepadSupport option is turned off (see common/widescreen_ini.h).
NFSMW_PLUGIN_EARLY() {
    char own_ini[MAX_PATH];
    nfsmw::data_file("mets_controller_support.ini", own_ini, sizeof(own_ini));
    if (GetPrivateProfileIntA("pad", "enabled", 1, own_ini) == 0) return NFSMW_OK;
    pad::new_controllers::install_early();
    if (nfsmw::widescreen_ini::find_path(g_widescreen_ini, sizeof(g_widescreen_ini)))
        g_widescreen_change = nfsmw::widescreen_ini::turn_off_gamepad_support(g_widescreen_ini);
    return NFSMW_OK;
}

NFSMW_PLUGIN_MAIN() {
    using namespace pad;
    g_log.open_in_data_folder("mets_controller_support.log");
    log_line("mets_controller_support 1.0.0 starting");
    if (g_widescreen_change == nfsmw::widescreen_ini::Change::kTurnedOff)
        log_line("widescreen fix: ImproveGamepadSupport set to 0 in %s (its controller pictures replace the keyboard's for good "
                 "and clash with ours)", g_widescreen_ini);
    else if (g_widescreen_change == nfsmw::widescreen_ini::Change::kFailed)
        log_line("widescreen fix: ImproveGamepadSupport is on in %s and could not be changed; set it to 0 by hand", g_widescreen_ini);
    load_settings();
    if (!g_cfg.enabled) return NFSMW_OK;
    xinput::load(&g_log);
    bindings::prepare_default_look_entries();
    menu_buttons::patch_default_table();
    default_layout::patch_default_table();
    input::install_hooks();
    bindings::install_hooks();
    new_controllers::install_hooks();
    pad_sets::install_hooks();
    look::install_hooks();
    button_icons::install_hooks();
    rumble::install_hooks();
    world_map::install_hooks();
    showroom::install_hooks();
    rumble::start();
    return NFSMW_OK;
}

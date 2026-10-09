/*
 * nfsmw_controller_support: controller input and rumble for speed.exe 1.3 (see README.md). Entry point: the parts are in their own
 * files, each with its own header and namespace:
 *   settings.cpp       nfsmw_controller_support.ini and the log
 *   input.cpp          DirectInput state (dead zones, separate triggers, mouse) and the analog action mapping
 *   bindings.cpp       the PC binding table: the camera look entries, a log of binding changes
 *   controls_menu.cpp  Options > Controls: the look rows, readable names, the title icon
 *   look.cpp           camera look
 *   rumble.cpp         rumble (the force feedback events, the controller's motors)
 *   rumble_page.cpp    Options > Rumble, a page of nfsmw_custom_menus
 */
#include "bindings.h"
#include "controls_menu.h"
#include "input.h"
#include "look.h"
#include "pad.h"
#include "rumble.h"

NFSMW_PLUGIN_DECLARE("NFSMW Pad", "1.0.0", "Demeter")

NFSMW_PLUGIN_MAIN() {
    using namespace pad;
    g_log.open_in_data_folder("nfsmw_controller_support.log");
    log_line("nfsmw_controller_support 1.0.0 starting");
    load_settings();
    if (!g_cfg.enabled) return NFSMW_OK;
    xinput::load(&g_log);
    bindings::prepare_default_look_entries();
    if (bindings::g_look_entries_ok) controls_menu::add_rows();
    input::install_hooks();
    bindings::install_hooks();
    controls_menu::install_hooks();
    look::install_hooks();
    rumble::install_hooks();
    rumble::start();
    return NFSMW_OK;
}

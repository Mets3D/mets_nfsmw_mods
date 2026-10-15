// What this mod adds to the Options menu, through nfsmw_custom_menus (see custom_menus_api.h): the pages and rows are
// asked for once, the first time the Options menu opens.
#include "bindings.h"
#include "controls_options.h"
#include "default_layout.h"
#include "custom_menus_api.h"
#include "controller_page.h"

extern "C" __declspec(dllexport) void nfsmw_custom_menus_provide(const nfsmw::custom_menus::Host *host) {
    char translations[MAX_PATH];
    nfsmw::data_file("translations\\controller_support", translations, sizeof(translations));
    host->register_translations(translations);  // before any text is registered
    pad::controller_page::provide(host);      // the Controller Options page
    pad::bindings::provide_controls_rows(host);  // Options > Controls: the look rows
    pad::controls_options::provide(host);         // Options > Controls: the hint opens the Controller Options page
    pad::default_layout::provide(host);          // the default bindings after the game resets them
}

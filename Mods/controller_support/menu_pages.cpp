// What this mod adds to the Options menu, through nfsmw_custom_menus (see custom_menus_api.h): the pages and rows are
// asked for once, the first time the Options menu opens.
#include "bindings.h"
#include "default_layout.h"
#include "custom_menus_api.h"
#include "prompts_option.h"
#include "rumble_page.h"

extern "C" __declspec(dllexport) void nfsmw_custom_menus_provide(const nfsmw::custom_menus::Host *host) {
    pad::rumble_page::provide(host);      // Options > Rumble
    pad::prompts_option::provide(host);   // Options > Player > Controller Prompts
    pad::bindings::provide_controls_rows(host);  // Options > Controls: the look rows
    pad::default_layout::provide(host);          // the default bindings after the game resets them
}

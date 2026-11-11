// Options > Player > Minimap: each player can turn their own minimap off.
#pragma once

namespace nfsmw::custom_menus {
struct Host;
}

namespace split_screen_support::minimap_option {

void register_page(const nfsmw::custom_menus::Host *host);

void install();

}  // namespace split_screen_support::minimap_option

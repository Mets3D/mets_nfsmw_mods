// Options > Player > Controller Prompts: which controller's buttons the menus' prompts show. A row in the game's own
// Player options, added through nfsmw_custom_menus.
#pragma once

#include "custom_menus_api.h"

namespace pad::prompts_option {

void provide(const nfsmw::custom_menus::Host *host);

}  // namespace pad::prompts_option

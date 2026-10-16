// The title icon of an Options screen (the small picture next to the screen's title).
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace custom_menus::title_icon {

constexpr unsigned kObject = 0x08007B4C;  // the icon object of the Options layouts (and the Controls screen's)

// Shows the texture `texture` as the title icon of the screen whose layout package is `pkg`.
inline void set(const char *pkg, unsigned texture) {
    const auto FEngSetTextureHashPkg = reinterpret_cast<void (*)(const char *, unsigned, unsigned)>(0x527EE0);
    FEngSetTextureHashPkg(pkg, kObject, texture);
}

// Hooks the Controls screen so it shows its own icon. Installed on its own: nothing else depends on it.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace custom_menus::title_icon

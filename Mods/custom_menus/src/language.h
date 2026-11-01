// Options > Player > Language: changes the game's language while it runs (SetCurrentLanguage, the function that sets it at start-up).
#pragma once

#include "custom_menus_api.h"
#include "nfsmw_util.h"

namespace custom_menus::language {

// Registers the row (a page with Placement kPlayerOptions) with the framework. `changed` is called after the game switched language, to
// translate and add our texts again.
// The save system's strings load from memcard/LOCALE_<LANGUAGE>.loc, which a language may lack (Redux v3 has English and Russian only): the English file is loaded then,
// where the game crashed. Installed at start-up, before the language is set.
void install_locale_fallback(nfsmw::Logger *log);

void provide(const nfsmw::custom_menus::Host *host, nfsmw::Logger *log, void (*changed)());

}  // namespace custom_menus::language

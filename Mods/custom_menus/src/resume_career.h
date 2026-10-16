// The main menu's Career entry resumes the career directly, instead of opening the Career main menu.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace custom_menus::resume_career {

// `[features] skip_career_menu=0` turns it off.
void install_hooks(nfsmw::Logger &log, const char *ini);

// Called for every option added to an icon menu (IconScrollerMenu::AddOption): remembers the main menu's Career entry.
void option_added(void *menu, void *option);

// Called before a screen switch is queued: true for the one the main menu makes after a resume (it must not happen).
bool swallow_switch(const char *pkg);

// Called before a screen switch is queued: the package to switch to instead (the safe house's Back goes to the main menu).
const char *redirect_switch(const char *pkg);

}  // namespace custom_menus::resume_career

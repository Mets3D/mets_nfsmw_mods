// A guard for a crash in the Options screens: the game measures (FEngSetCenter -> FEngFont::GetTextWidth) and draws
// (cFEngRender::RenderString) a text object that has no text, and reads a null string. Seen on Redux v3's Video screen layout,
// whose rows have no text until the widgets set it, and which our pages are built on.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace custom_menus::text_guard {

// Makes GetTextWidth treat a null string as an empty one, and a text object without text not drawn; logs the first two times
// each happens.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace custom_menus::text_guard

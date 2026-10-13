// Extra rows on the Controls screen (UIOptionsPCController), for the mods that own binding entries (see ControlsRow in
// custom_menus_api.h): the screen's code is patched to make room, and the helpers that turn a row into a binding entry are
// hooked to translate the added rows. Header and implementation in this part; the registrations come from the mods' provide exports.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "custom_menus_api.h"
#include "nfsmw_util.h"

namespace custom_menus::controls_rows {

void add_rows(const nfsmw::custom_menus::ControlsRow *rows, int count);
void add_text(unsigned hash, const char *text);
void set_binding_label(nfsmw::custom_menus::BindingLabelFn fn);
void add_refresh(void (*fn)());

// Once every provider has registered: patches the screen's code and hooks the helpers, if there are rows.
void apply(nfsmw::Logger &log, const char *ini);

}  // namespace custom_menus::controls_rows

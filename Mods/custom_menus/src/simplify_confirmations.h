// "Are you sure you want to exit without accepting changes? All changes will be lost." (Yes lost the changes) and the Controls menu's "Do you
// wish to keep the modified settings? ..." become one question, "Save changes?": Yes keeps the changes and leaves, No loses them and leaves,
// Back stays.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace custom_menus::simplify_confirmations {

// `[features] simplify_confirmations=0` turns it off. Call before skip_dialogs::install_hooks, which asks enabled().
void install_hooks(nfsmw::Logger &log, const char *ini);
bool enabled();

// From the ShowDialog hook, before the dialog is shown: rewrites the config of the dialogs above.
void rewrite(void *config);

}  // namespace custom_menus::simplify_confirmations

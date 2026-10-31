// FEngGetLastButton (0x571E80) returns the entry an icon menu was left from as a byte: it loads it into al over the pointer to the
// screen's record still in eax (0 when the screen has none). The game's callers zero-extend al; a plugin of Redux v3 that sets up the race
// type menu (MainMenu_Sub.fng in mode select, called from 0x715D3F0A in a test run) passes all of eax, e.g. 0x918303 for the third race type,
// which IconScroller::SetInitialPos takes as no entry: the menu started on Circuit. Here the whole register is the byte.
#include "last_button.h"

#include "hooks.h"

namespace custom_menus::last_button {

namespace {

using GetLastButtonFn = unsigned (*)(const char *);
GetLastButtonFn g_orig_get_last_button;

unsigned hook_get_last_button(const char *package) { return g_orig_get_last_button(package) & 0xFF; }

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    if (!GetPrivateProfileIntA("features", "remember_menu_entry", 1, ini)) return;
    const nfsmw::Hook hooks[] = {
        {"lastbutton", "FEngGetLastButton", 0x571E80, {0x8B, 0x44, 0x24, 0x04, 0x53, 0x50, 0xE8}, false, nfsmw::detour(&hook_get_last_button),
         nfsmw::orig(&g_orig_get_last_button)},
    };
    nfsmw::install_hooks(hooks, 1, log, ini, "last button");
}

}  // namespace custom_menus::last_button

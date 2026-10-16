// A base game bug: the Controls screen (UIOptionsPCController, Pause > Options > Controls) shows the audio icon as its
// title icon. Its layout (Pause_PC_Controller.fng) defaults the icon object to the audio icon, like the Options layout
// does, but unlike the other Options screens this one never sets its own. Fixed here, for everyone, so that no mod that
// changes the Controls screen needs to.
#include "title_icon.h"

#include "hooks.h"

namespace custom_menus::title_icon {

namespace {

constexpr unsigned kControlsIconTexture = 0xA04A7B26;  // OPTIONS_ICON_CONTROLS
constexpr unsigned kScreenPackage = 0x10;              // the screen's layout package name

// UIOptionsPCController::Setup, called right before the constructor's first refresh of the rows; the layout is
// loaded by then.
using SetupFn = void(FASTCALL *)(void *, void *);
SetupFn g_orig_setup;
void FASTCALL hook_setup(void *screen, void *edx) {
    g_orig_setup(screen, edx);
    if (const char *pkg = nfsmw::field<const char *>(screen, kScreenPackage)) set(pkg, kControlsIconTexture);
}

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    const nfsmw::Hook hooks[] = {
        {"controlsicon", "UIOptionsPCController::Setup", 0x52A7B0, {0x6A, 0xFF, 0x68, 0x38, 0xEF, 0x86, 0x00}, false,
         nfsmw::detour(&hook_setup), nfsmw::orig(&g_orig_setup)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "Controls screen icon");
}

}  // namespace custom_menus::title_icon

// The game's icon strips (IconScroller, the Options of IconScrollerMenu) already know how to wrap: the panel has a wrap flag (+0x28 in the
// PC build) that left / right check, and IconScroller::ScrollWrapped selects the last icon from the first and the other way round; every
// strip is made with the flag off. So the flag is set when a strip is built. The strip then slides across all its icons the way it does for
// any other selection.
#include "cycle_strips.h"

#include "hooks.h"

namespace custom_menus::cycle_strips {

namespace {

constexpr unsigned kWrap = 0x28;

nfsmw::Logger *g_log = nullptr;

// IconScroller::IconScroller(package, master, button, scroll region, width): thiscall, returns the object. The width is passed on as bits.
using CtorFn = void *(FASTCALL *)(void *, void *, const char *, const char *, const char *, const char *, unsigned);
CtorFn g_orig_ctor;
void *FASTCALL hook_ctor(void *self, void *edx, const char *package, const char *master, const char *button, const char *region, unsigned width) {
    void *result = g_orig_ctor(self, edx, package, master, button, region, width);
    nfsmw::field<bool>(self, kWrap) = true;
    static bool said = false;
    if (!said) g_log->line("strip menus cycle (first: %s)", package ? package : "?");
    said = true;
    return result;
}

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    if (!GetPrivateProfileIntA("features", "cycling_strip_menus", 1, ini)) return;
    g_log = &log;
    const nfsmw::Hook hooks[] = {
        {"stripctor", "IconScroller::IconScroller", 0x59B8A0, {0x6A, 0xFF, 0x68, 0x48, 0x2E, 0x87, 0x00}, false,
         nfsmw::detour(&hook_ctor), nfsmw::orig(&g_orig_ctor)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "strip menus");
}

}  // namespace custom_menus::cycle_strips

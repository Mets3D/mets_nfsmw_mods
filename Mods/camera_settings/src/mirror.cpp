#include "mirror.h"

#include "hooks.h"
#include "patch.h"
#include "view_flags.h"

namespace camera_settings::mirror {

namespace {

using nfsmw::field;

nfsmw::Logger *g_log = nullptr;

// Two things decide whether the mirror is drawn (the view mode 2, "one view and the mirror", set by MaybeChangeViewMode 0x6CFAD0 every
// frame): FEngHud::ShouldRearViewMirrorBeVisible(view id) 0x595D40, which refuses the views Close to Pursuit (CurCam 2-6) and
// everything else that hides the mirror (the Rearview option, pause, the HUD hidden, looking back); and the camera mover's
// OutsidePOV, true for those same views. For a view the player gave a Mirror setting we answer the first ourselves, and the
// second only when MaybeChangeViewMode asks (OutsidePOV is also asked for other things, which stay as they were).
constexpr uintptr_t kFEDatabase = 0x91CF90;  // FEDatabase*; +0x10 = the options settings
constexpr unsigned kSettings = 0x10, kCurCamOfView = 0x78;  // the CurCam of view id v (1 = the first player) is at 0x78 + v * 0x14
constexpr uintptr_t kViewModeFn = 0x6CFAD0, kViewModeSize = 0x20E;  // MaybeChangeViewMode

// The CurCam of a view's player, or null for a view that has none (the mirror's own, split screen's others).
int *cur_cam(int view) {
    if (view < 1 || view > 2) return nullptr;
    void *database = *reinterpret_cast<void **>(kFEDatabase);
    void *settings = database ? field<void *>(database, kSettings) : nullptr;
    return settings ? &field<int>(settings, kCurCamOfView + view * 0x14) : nullptr;
}

// The game's Gameplay > Rearview Mirror option (RearviewOn, the byte at +0x39 of the options settings) is replaced by the per-view
// Mirror setting, when the Camera page is there to set it (nfsmw_custom_menus): its row is left out of the Gameplay screen
// and the value is kept on, so everything that reads it (the mirror's visibility, the minimap and radar detector checks of the
// view mode) behaves as with the option on. Without the page the game's own option stays.
constexpr unsigned kRearviewOn = 0x39;
constexpr uintptr_t kGORearviewVtable = 0x89BF1C;

using AddToggleFn = unsigned(FASTCALL *)(void *, void *, void *, int);
AddToggleFn g_orig_add_toggle;  // null when the row's hook is not installed (then the game's option stays in charge)

bool option_replaced() {
    if (!g_orig_add_toggle) return false;
    static bool found = false;
    found = found || GetModuleHandleA("nfsmw_custom_menus.asi") != nullptr;  // it may load after this plugin
    return found;
}

void keep_rearview_on() {
    void *database = *reinterpret_cast<void **>(kFEDatabase);
    void *settings = database ? field<void *>(database, kSettings) : nullptr;
    if (settings && option_replaced()) field<unsigned char>(settings, kRearviewOn) = 1;
}

bool outside_view(int cam) { return cam >= 2 && cam <= 6; }

using VisibleFn = bool (*)(int);
VisibleFn g_orig_visible;

bool hook_visible(int view) {
    keep_rearview_on();
    int *cam = cur_cam(view);
    const int shown = cam ? mirror_override(*cam) : -1;
    if (shown == 0) return false;
    if (shown == 1 && outside_view(*cam)) {
        // Everything else the game checks stays: ask it as for the bumper view.
        const int real = *cam;
        *cam = 0;
        const bool visible = g_orig_visible(view);
        *cam = real;
        return visible;
    }
    return g_orig_visible(view);
}

using OutsideFn = bool(FASTCALL *)(void *, void *);
OutsideFn g_orig_outside;

__attribute__((noinline)) bool FASTCALL hook_outside(void *self, void *edx) {
    const bool outside = g_orig_outside(self, edx);
    if (!outside) return false;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    if (caller < kViewModeFn || caller >= kViewModeFn + kViewModeSize) return true;
    // MaybeChangeViewMode looks at the first player's camera mover only.
    int *cam = cur_cam(1);
    return !(cam && mirror_override(*cam) == 1 && hook_visible(1));
}

// UIWidgetMenu::AddToggleOption(screen, widget, enabled): the Gameplay screen's Rearview Mirror row (a GORearview) is not added.
unsigned FASTCALL hook_add_toggle(void *screen, void *edx, void *widget, int enabled) {
    if (widget && field<uintptr_t>(widget, 0) == kGORearviewVtable && option_replaced()) {
        static bool logged = false;
        if (!logged) g_log->line("mirror: the Gameplay screen's Rearview Mirror option is left out (it is a setting of each camera view now)");
        logged = true;
        return 0;
    }
    return g_orig_add_toggle(screen, edx, widget, enabled);
}

}  // namespace

// NFSMWHDReflections' ForceEnableMirror replaces the `je` at 0x595DDD (the first of the compares of CurCam 2-6 in ShouldRearViewMirrorBeVisible,
// which hide the mirror in the outside views) by `jmp +0x14`, over all of them. main.cpp's early entry turns the option off in its ini, which
// works when our plugin is loaded first; if that patch is there anyway, the original bytes are put back (the game's behaviour, which
// the Mirror settings are relative to).
void undo_force_enable_mirror(nfsmw::Logger &log) {
    constexpr uintptr_t kCompares = 0x595DDD;
    const unsigned char forced[2] = {0xEB, 0x14}, original[2] = {0x74, 0x2D};
    if (nfsmw::patch_bytes(kCompares, forced, original, 2)) log.line("mirror: the forced mirror of NFSMWHDReflections was undone");
}

void install_hooks(nfsmw::Logger &log, const char *ini) {
    g_log = &log;
    if (GetPrivateProfileIntA("hooks", "mirrorvisible", 1, ini)) undo_force_enable_mirror(log);
    const nfsmw::Hook hooks[] = {
        {"mirrorvisible", "FEngHud::ShouldRearViewMirrorBeVisible", 0x595D40, {0xA1, 0x84, 0xD8, 0x92, 0x00, 0x53, 0x8B}, false,
         nfsmw::detour(&hook_visible), nfsmw::orig(&g_orig_visible)},
        {"outsidepov", "CubicCameraMover::OutsidePOV", 0x46AFA0, {0x8B, 0x81, 0x9C, 0x00, 0x00, 0x00, 0x83}, false,
         nfsmw::detour(&hook_outside), nfsmw::orig(&g_orig_outside)},
        {"rearviewrow", "UIWidgetMenu::AddToggleOption", 0x588570, {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x8B}, false,
         nfsmw::detour(&hook_add_toggle), nfsmw::orig(&g_orig_add_toggle)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "mirror");
}

}  // namespace camera_settings::mirror

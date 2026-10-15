#include "camera_cycle.h"

#include "hooks.h"
#include "view_flags.h"

namespace camera_settings::camera_cycle {

namespace {

using nfsmw::field;

constexpr unsigned kCurCam = 0xC;  // PlayerSettings
constexpr int kViews = 7;

// ScrollDriveCam(settings, +1 or -1) is only called from InputPlayer::FetchInput (the Change Camera View action, and our Previous
// Camera View), never by the Options screen's selector, which therefore still reaches the disabled views. The game's step
// already skips the views it does not allow; ours repeats it past the disabled ones. With every view disabled it stays put.
using ScrollFn = void(FASTCALL *)(void *, void *, int);
ScrollFn g_orig_scroll;

void FASTCALL hook_scroll(void *settings, void *edx, int direction) {
    int &cam = field<int>(settings, kCurCam);
    const int start = cam;
    for (int step = 0; step < kViews; ++step) {
        g_orig_scroll(settings, edx, direction);
        if (in_cycle(cam)) return;
    }
    cam = start;
}

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    const nfsmw::Hook hooks[] = {
        {"scroll", "PlayerSettings::ScrollDriveCam", 0x5A3850, {0x8B, 0x44, 0x24, 0x04, 0x83, 0xF8, 0x01}, false,
         nfsmw::detour(&hook_scroll), nfsmw::orig(&g_orig_scroll)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "camera cycle");
}

}  // namespace camera_settings::camera_cycle

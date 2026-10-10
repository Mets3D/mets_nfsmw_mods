/*
 * The track streamer (TheTrackStreamer, 0x9AA9D0) loads the world in sections around two streaming positions (entries
 * of 0x64 bytes at +0xC0, "set" at +0x1C), and in a split screen game (its flag at +0xB0, from InitRegion) it keeps the
 * sections of both. The positions come from UpdateCameraMovers (0x47C880), once per frame: the streamer's positions
 * are cleared, then each of views 1 and 2 that is active and has a camera mover gives its camera's position (+0x40),
 * velocity (+0x200) and direction (+0x50) to PredictStreamingPosition (0x7238D0; position 0 for view 1, 1 for view 2;
 * last argument: the camera follows the car, the first mover's type +0xC == 1).
 *
 * View 2 is never active on PC (it has no render target, and an active view without one crashes the per-view setup),
 * so only player 1's surroundings were loaded. After UpdateCameraMovers this gives position 1 from view 2's camera,
 * the same way, unless the game skipped the update (the same two checks it makes).
 */
#include "streaming.h"

#include "hooks.h"
#include "split_screen_support.h"

namespace split_screen_support::streaming {

namespace {

constexpr uintptr_t kTrackStreamer = 0x9AA9D0;
constexpr uintptr_t kView2 = 0x9196C0;
constexpr uintptr_t kViewMode = 0x982B40;  // the current EVIEWMODE
constexpr int kTwoViews = 3;

using PredictFn = void(__fastcall *)(void *streamer, void *, int position_number, const void *position, const void *velocity,
                                     const void *direction, int following_car);

template <typename T>
T at(uintptr_t base, int offset) {
    return *reinterpret_cast<T *>(base + offset);
}

// What UpdateCameraMovers checks before it updates the positions (a warp, a script waiting for loading).
bool game_skips_update() {
    const uintptr_t a = at<uintptr_t>(0x91E00C, 0), b = at<uintptr_t>(0x91E000, 0);
    return (a && at<unsigned char>(a, 0x2B4)) || (b && at<unsigned char>(b, 0x19A4));
}

void (*g_orig_update_camera_movers)(int);
void hook_update_camera_movers(int arg) {
    g_orig_update_camera_movers(arg);
    if (*reinterpret_cast<int *>(kViewMode) != kTwoViews || game_skips_update()) return;
    const uintptr_t camera = at<uintptr_t>(kView2, 0x40);
    const uintptr_t list = kView2 + 0x44, first = at<uintptr_t>(list, 0);
    if (!camera || !first || first == list) return;  // no camera mover
    const uintptr_t mover = first - 4;
    const int following = at<int>(mover, 0xC) == 1;
    reinterpret_cast<PredictFn>(0x7238D0)(reinterpret_cast<void *>(kTrackStreamer), nullptr, 1,
                                          reinterpret_cast<void *>(camera + 0x40), reinterpret_cast<void *>(camera + 0x200),
                                          reinterpret_cast<void *>(camera + 0x50), following);
    static bool logged = false;
    if (!logged) {
        logged = true;
        g_log.line("streaming: player 2's position given to the track streamer (its split screen flag: %d)",
                   at<unsigned char>(kTrackStreamer, 0xB0));
    }
}

}  // namespace

void install() {
    const nfsmw::Hook hooks[] = {
        {"cameramovers", "UpdateCameraMovers", 0x47C880, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83}, false,
         nfsmw::detour(&hook_update_camera_movers), nfsmw::orig(&g_orig_update_camera_movers)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "streaming");
}

}  // namespace split_screen_support::streaming

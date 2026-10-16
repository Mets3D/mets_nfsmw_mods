#include "tilt.h"

#include <algorithm>

#include "hooks.h"
#include "patch.h"
#include "view_flags.h"

namespace camera_settings::tilt {

namespace {

using nfsmw::field;

// In the outside views the camera's up direction is (x, y, 1) with x and y taken from a spring (the mover's +0x94) that moves toward
// a target set at 0x47D270 inside the placement code sub_47CF00 (called from the mover's update): world up (0, 0, 1), plus a lean
// taken from the acceleration when the POV allows tilting. We replace that target with the lean that makes the camera's up the car's up: the third row of the
// car's matrix, which the camera anchor (the mover's +0x80) keeps at +0x50 (row 2 = +0x70), as (x / z, y / z), by a share.
// The bumper and hood views use the car's up directly and are not affected.
constexpr unsigned kType = 0x9C, kAnchor = 0x80, kCarUp = 0x70;
constexpr uintptr_t kPlacement = 0x47CF00, kTargetCall = 0x47D270, kSetTarget = 0x651790;
constexpr float kMinUpZ = 0.3f;  // upside down or on its side the lean would run away: about 73 degrees at most

void *g_mover = nullptr;  // the mover whose placement is running

// sub_47CF00(mover, four stack arguments): only the mover is remembered.
using PlaceFn = void(FASTCALL *)(void *, void *, void *, void *, void *, void *);
PlaceFn g_orig_place;

void FASTCALL hook_place(void *mover, void *edx, void *a, void *b, void *c, void *d) {
    g_mover = mover;
    g_orig_place(mover, edx, a, b, c, d);
    g_mover = nullptr;
}

using SetTargetFn = void(FASTCALL *)(void *, void *, float *);

void FASTCALL set_up_target(void *spring, void *edx, float *up) {
    float lean[3] = {up[0], up[1], up[2]};
    if (g_mover) {
        const float share = tilt_factor(field<int>(g_mover, kType));
        void *anchor = field<void *>(g_mover, kAnchor);
        if (anchor) {
            const float x = field<float>(anchor, kCarUp), y = field<float>(anchor, kCarUp + 4), z = field<float>(anchor, kCarUp + 8);
            const float z_safe = std::max(z, kMinUpZ);
            lean[0] = share * x / z_safe;
            lean[1] = share * y / z_safe;
            lean[2] = 1.0f;
        }
    }
    reinterpret_cast<SetTargetFn>(kSetTarget)(spring, edx, lean);
}

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    const nfsmw::Hook hooks[] = {
        {"tilt", "CubicCameraMover placement (sub_47CF00)", kPlacement, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81}, false,
         nfsmw::detour(&hook_place), nfsmw::orig(&g_orig_place)},
    };
    if (nfsmw::install_hooks(hooks, 1, log, ini, "tilt").created != 1) return;
    // The call to the spring's target setter: E8 rel32 to 0x651790 becomes a call to ours.
    unsigned char expected[5] = {0xE8}, patch[5] = {0xE8};
    const int32_t was = static_cast<int32_t>(kSetTarget - (kTargetCall + 5));
    const int32_t now = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&set_up_target) - (kTargetCall + 5));
    memcpy(expected + 1, &was, 4);
    memcpy(patch + 1, &now, 4);
    log.line("tilt: %s", nfsmw::patch_bytes(kTargetCall, expected, patch, 5)
                             ? "the camera's up direction follows the car's tilt by the Tilt setting"
                             : "the up direction's call was changed by another mod: left alone, Tilt does nothing");
}

}  // namespace camera_settings::tilt

#include "acceleration.h"

#include "hooks.h"
#include "view_flags.h"

namespace camera_settings::acceleration {

namespace {

using nfsmw::field;

// CubicCameraMover::Update calls CameraAccelCurve(mover, vec) to turn the car's acceleration (the anchor's +0x40, +0x44, +0x48) into
// an offset: each component goes through its own curve table (0x8EDE54...) and a constant. sub_478B70 then averages the offsets over
// the last few frames and keeps the average (the mover's +0xE0 object, +0x20), which sub_47CF00 turns into camera space and adds
// to the eye and the target (the camera pulls back under power, pushes ahead under braking) and uses to lean the up vector (the
// "tilting"). Scaling the offset scales all of that; 1 is the game's own.
constexpr unsigned kType = 0x9C;

using CurveFn = void(FASTCALL *)(void *, void *, float *);
CurveFn g_orig_curve;

void FASTCALL hook_curve(void *mover, void *edx, float *vec) {
    g_orig_curve(mover, edx, vec);
    const int type = field<int>(mover, kType);
    const float factor = acceleration_factor(type);
    if (factor == 1.0f) return;
    for (int i = 0; i < 3; ++i) vec[i] *= factor;
}

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    const nfsmw::Hook hooks[] = {
        {"acceleration", "CubicCameraMover::CameraAccelCurve", 0x47CDD0, {0x83, 0xEC, 0x14, 0x53, 0x55, 0x56, 0x8B}, false,
         nfsmw::detour(&hook_curve), nfsmw::orig(&g_orig_curve)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "acceleration");
}

}  // namespace camera_settings::acceleration

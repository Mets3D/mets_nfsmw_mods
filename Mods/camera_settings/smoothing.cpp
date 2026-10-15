#include "smoothing.h"

#include <cstring>

#include "hooks.h"
#include "view_flags.h"

namespace camera_settings::smoothing {

namespace {

// CubicCameraMover::Update (and its constructor) moves the camera with tCubic springs: each goes from where it is to its target
// as a cubic over a duration (tCubic1D +0x24, in seconds; it advances by dt / duration). The durations come from a speed-keyed
// tweak table, one entry of 5 words per view type at 0x8EE040, sampled by sub_46F2B0(table entry, out, speed share) into 44
// floats: [0..2] are the durations of the springs +0x8C, +0x84 and +0x94 / +0x88, [41..43] (plus values from the car) those of +0x90.
// Scaling them scales how slowly the camera follows; 1 leaves the game's behaviour. Only those two callers exist.
constexpr uintptr_t kTable = 0x8EE040;
constexpr unsigned kEntrySize = 0x14, kWords = 44;
constexpr int kDurations[] = {0, 1, 2, 41, 42, 43};

nfsmw::Logger *g_log = nullptr;

using SampleFn = void(FASTCALL *)(void *, void *, float *, float);
SampleFn g_orig_sample;

void FASTCALL hook_sample(void *table, void *edx, float *out, float speed_share) {
    g_orig_sample(table, edx, out, speed_share);
    const int type = static_cast<int>((reinterpret_cast<uintptr_t>(table) - kTable) / kEntrySize);
    if (type < 0 || type > 6) return;
    // One line per view type: what the game samples (for working out what the numbers are), at the first speed seen.
    static bool dumped[7];
    if (!dumped[type]) {
        dumped[type] = true;
        char text[640] = {};
        size_t used = 0;
        for (unsigned i = 0; i < kWords && used < sizeof(text) - 16; ++i) used += snprintf(text + used, sizeof(text) - used, " %g", out[i]);
        g_log->line("smoothing: view %d tweaks at speed share %.2f:%s", type, speed_share, text);
    }
    const float factor = smoothing_factor(type);
    if (factor == 1.0f) return;
    for (int i : kDurations) out[i] *= factor;
}

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    g_log = &log;
    const nfsmw::Hook hooks[] = {
        {"smoothing", "CubicCameraMover tweak sampler", 0x46F2B0, {0x51, 0xD9, 0x44, 0x24, 0x0C, 0x56, 0x8B}, false,
         nfsmw::detour(&hook_sample), nfsmw::orig(&g_orig_sample)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "smoothing");
}

}  // namespace camera_settings::smoothing

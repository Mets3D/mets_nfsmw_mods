// Declarations shared by the parts of mets_controller_support: the log, the settings and a few small helpers. Each part has its own
// header (input.h, bindings.h, ...) and lives in its own namespace.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include <cmath>

#include "nfsmw_util.h"
#include "xinput.h"

namespace pad {

using nfsmw::field;
namespace xinput = nfsmw::xinput;

using MethodFn = void(FASTCALL *)(void *, void *);  // a hooked thiscall method without arguments

extern nfsmw::Logger g_log;
void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// log_line is for what a bug report needs: the setup and what the user did.

inline float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
inline float ramp(float x, float lo, float hi) { return clamp01((x - lo) / (hi - lo)); }

// ---- settings: mets_controller_support.ini ----------------------------------------------------------------------------------
extern char g_ini[MAX_PATH];
float ini_float(const char *key, float def, const char *section = "pad");

struct Settings {  // [pad]
    bool enabled = true;
    bool analog_input = true;  // steering, gas and brake are analog
    bool split_triggers = true;  // LT and RT as two axes
    bool use_all_xinput_controllers = true;  // every XInput controller drives the game, not just the first one DirectInput gave it
};
extern Settings g_cfg;

struct LookSettings {  // [look]
    bool enabled = true;
    float stick_speed = 150.0f;       // degrees per second at full deflection
    float mouse_sensitivity = 0.15f;  // degrees per mouse count
    bool invert_y = false;
    float return_delay = 3.0f;  // seconds without look input before the view returns
    float return_speed = 2.0f;
    float min_pitch = -10.0f;  // the view's height angle limits, in degrees above the car
    float max_pitch = 60.0f;
};
extern LookSettings g_look_cfg;

void load_settings();  // all of the above, and the rumble levels

}  // namespace pad

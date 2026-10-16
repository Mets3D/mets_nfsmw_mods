/*
 * nfsmw_camera_settings: per-preset camera settings for speed.exe 1.3 (SHA-256 0C5675A0...2067E342).
 *
 *  - CameraAnchor::GetPov builds the active camera preset (POV) from the car's attributes every frame. Our
 *    per-preset changes (differences from the car's own values) are applied to what it returns, so switching views with the game's hotkey keeps them.
 *  - Pause > Options > Camera (a page of nfsmw_custom_menus, which builds the screen): row 1 is the game's own Camera
 *    selector, the rows below edit the selected view (hold Shift or LB for finer steps). Edits are kept on Accept.
 *  - Previous Camera View (previous_camera.cpp): a Controls screen binding that scrolls the camera backwards; the game's Change
 *    Camera View is renamed Next Camera View.
 *  - Enabled (camera_cycle.cpp): Next / Previous Camera View skip the views switched off. Mirror (mirror.cpp): the rear view mirror
 *    shown or hidden per view.
 *  - Smoothing (smoothing.cpp): the durations of the chase camera's springs, scaled per view.
 *  - Acceleration (acceleration.cpp): how far the camera moves and leans with the car's acceleration, per view.
 *  - Tilt (tilt.cpp): how far the chase camera's horizon follows the car's own tilt, per view.
 *  - The Drift camera's name ("DRIFT", from NFSMWExtraOptions) is shown with the game's own "Drift" string.
 *
 * Files in <game>/scripts/mets_mods/: nfsmw_camera_settings.ini (your changes and settings), nfsmw_camera_settings.log, nfsmw_camera_settings.tpk (the Options icon).
 * ini settings, all optional:
 *   [general]  degree_symbol=0  writes " deg" instead of the degree sign
 *              icon=<texture name, or its hash as 8 hex digits>  the Options icon (default OPTIONS_CAMERA)
 *   [hooks]    <name>=0  skips one hook (names in install_hooks)
 *
 * Offsets come from the PC disassembly; the decomp's struct offsets are from the console builds and differ.
 */

#include <nfsmw_sdk/nfsmw_sdk.h>
#include <MinHook.h>

#include <cctype>
#include <cmath>
#include <cstdlib>

#include "hooks.h"
#include "nfsmw_util.h"
#include "custom_menus_api.h"
#include "acceleration.h"
#include "camera_cycle.h"
#include "mirror.h"
#include "previous_camera.h"
#include "smoothing.h"
#include "tilt.h"
#include "view_flags.h"

NFSMW_PLUGIN_DECLARE("NFSMW Camera Editor", "1.1.0", "Demeter Dzadik")

#define THISCALL __attribute__((thiscall))
// FASTCALL (__fastcall) comes from windows.h.

namespace {

using nfsmw::field;
using nfsmw::texture_hash;
namespace opt = nfsmw::custom_menus;

nfsmw::Logger g_log;

void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_line(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    g_log.vline(fmt, ap);
    va_end(ap);
}

// The game's functions and data we use (speed.exe 1.3).
namespace game {
const auto GetPOVTypeFromPlayerCamera = reinterpret_cast<int (*)(int)>(0x56EBC0);
const auto FEngSetLanguageHash = reinterpret_cast<void (*)(void *string, unsigned hash)>(0x515C00);
constexpr uintptr_t kFEDatabase = 0x91CF90;    // FEDatabase*; +0x10 = options settings
constexpr uintptr_t kPlayerToEdit = 0x91CA24;  // UIOptionsScreen::PlayerToEdit
const auto IsPaused = reinterpret_cast<bool(THISCALL *)(void *)>(0x64B680);  // GameFlowManager::IsPaused
constexpr uintptr_t kGameFlowManager = 0x925E70;
const auto CameraAnchorCtor = reinterpret_cast<void *(THISCALL *)(void *self, unsigned car_type_hash)>(0x4716A0);
constexpr uintptr_t kCarTypeInfoArray = 0x9B09D8;  // CarTypeInfo*: 84 entries of 0xD0 bytes, the name at +0, its hash at +0x50
constexpr int kCarTypeCount = 84;
constexpr unsigned kCarTypeSize = 0xD0, kCarTypeNameHash = 0x50;
}  // namespace game

constexpr unsigned kRowData = 0x30;    // PODriveCam: FEString* of the value
constexpr unsigned kLabelHash = 0x60;  // FEString: language hash of its text
constexpr unsigned kCustomIconTexture = 0x12F6B313;  // OPTIONS_CAMERA, in our nfsmw_camera_settings.tpk
constexpr unsigned kDriftUpper = 0xA1211428;         // "DRIFT", NFSMWExtraOptions' name for the Drift camera
constexpr unsigned kDrift = 0x6AEB7EE6;              // "Drift", the game's own string

// ------------------------------------------------------------------------------------------------
// Camera presets
// ------------------------------------------------------------------------------------------------
// POV as built by GetPov at CameraAnchor+0xC8 (PC layout).
struct Pov {
    short type;            // 0x00  0..6
    unsigned short angle;  // 0x02  16-bit angle, 65536 = 360 deg (signed wrap)
    float lag;             // 0x04
    float height;          // 0x08
    float lat_offset;      // 0x0C
    unsigned short fov;    // 0x10  16-bit angle
    short pad;             // 0x12
    float stiffness;       // 0x14  never read by the game
    short tilting;         // 0x18  the game's lean with acceleration; tilt.cpp replaces what it leads to
};

enum Param { P_ENABLED, P_MIRROR, P_FOV, P_SPEED, P_SMOOTH, P_ACCEL, P_HEIGHT, P_LAG, P_ANGLE, P_TILT, NPARAM };
// On / off rows: their value is stored as it is (1 / 0), not as a difference from the game's own.
constexpr bool is_toggle(int i) { return i == P_ENABLED || i == P_MIRROR; }
constexpr int kNumPresets = 7;

const char *kPresetName[kNumPresets] = {"bumper", "hood", "close", "far", "super_far", "drift", "pursuit"};
const char *kKey[NPARAM] = {"enabled", "mirror", "fov", "speed_fov", "smoothing", "acceleration", "height", "lag", "angle", "tilt"};
const char *kLabel[NPARAM] = {"Enabled", "Mirror", "FOV", "Speed FoV", "Smoothing", "Acceleration", "Height", "Distance", "Pitch", "Tilt"};

// Help texts (2 key on the Camera options screen). Pitch is worded from the field name and values, so adjust it here if the
// description does not match what you see.
const char *kHelp[NPARAM + 1] = {
    "Whether Next and Previous Camera View stop at this view.",
    "Enable rear view mirror in this view.",
    "Field of view in degrees. Higher values show more of the scene and exaggerate the feeling of speed. "
    "Lower values zoom in. Hold Shift or LB for finer steps.",
    "Extra field of view in degrees, added on top of the base FoV as you speed up, to exaggerate the feeling of "
    "speed. It reaches this value at 300 km/h. 0 turns it off. Hold Shift or LB for finer steps.",
    "How much the camera's movement is smoothed. Hold Shift or LB for finer steps.",
    "How much the camera pulls back and pushes forward during acceleration and braking. Hold Shift or LB for finer steps.",
    "The camera's height above the car. Hold Shift or LB for finer steps.",
    "The camera's backwards/forwards offset from the car. Hold Shift or LB for finer steps.",
    "Tilts the camera up or down, in degrees. Hold Shift or LB for finer steps.",
    "How much the camera tilts with the car. Hold Shift or LB for finer steps.",
    "Selects the camera view whose settings you are editing. This is the same setting as the Camera option "
    "under Player.",
};

struct Range {
    float min, max, step, fine;  // fine: the step while Shift is held; values are kept on this grid
};
const Range kRange[NPARAM] = {
    {-1.0f, 1.0f, 2.0f, 2.0f},      // enabled
    {-1.0f, 1.0f, 2.0f, 2.0f},      // mirror
    {20.0f, 140.0f, 5.0f, 1.0f},    // FOV
    {0.0f, 50.0f, 5.0f, 1.0f},      // speed FoV, degrees at full speed (the game has no value of its own: base is 0)
    {10.0f, 400.0f, 10.0f, 1.0f},   // smoothing, percent of the game's (base 100)
    {0.0f, 300.0f, 10.0f, 1.0f},    // acceleration, percent of the game's (base 100)
    {0.0f, 8.0f, 0.25f, 0.05f},     // height
    {-30.0f, 3.0f, 0.5f, 0.1f},     // lag (negative = behind the car)
    {-20.0f, 20.0f, 0.5f, 0.1f},    // pitch
    {0.0f, 100.0f, 10.0f, 1.0f},    // tilt, percent of the car's (the game has none: base 0)
};

// What the player changed for one view. `val` is a difference from the game's own value for the car being driven (cars
// differ a little, and the game's values can also move with speed), except the on / off rows, which hold the 1 / 0 itself.
struct Override {
    bool set[NPARAM];
    float val[NPARAM];
};

Override g_ovr[kNumPresets];       // the changes in effect
Override g_ovr_open[kNumPresets];  // g_ovr when Options > Camera opened: what was last applied (and is in the ini)
float g_base[kNumPresets][NPARAM];  // the game's own values, captured before overriding
bool g_have_base[kNumPresets];
int g_cur_type = 3;                // preset most recently requested from GetPov
void *g_anchor = nullptr;          // CameraAnchor passed to GetPov (valid while a race is loaded)

bool g_degree_sign = true;              // [general] degree_symbol
unsigned g_icon = kCustomIconTexture;   // [general] icon

float ang_to_deg(unsigned short a) { return static_cast<short>(a) * 360.0f / 65536.0f; }
unsigned short deg_to_ang(float d) {
    return static_cast<unsigned short>(static_cast<int>(lroundf(d * 65536.0f / 360.0f)));
}

float get_field(const Pov &p, int i) {
    switch (i) {
        case P_FOV: return p.fov * 360.0f / 65536.0f;
        case P_HEIGHT: return p.height;
        case P_LAG: return p.lag;
        case P_ANGLE: return ang_to_deg(p.angle);
        default: return 0.0f;  // Speed FoV has no value of the game's; Enabled and Mirror are not in the Pov (read_base)
    }
}

// The game's own values of a view: the Pov's, and for the two on / off rows that are not in it, Enabled (always) and Mirror
// (the bumper and hood views have one).
void read_base(const Pov &p, int type, float *out) {
    for (int i = 0; i < NPARAM; ++i) out[i] = get_field(p, i);
    out[P_ENABLED] = 1.0f;
    out[P_SMOOTH] = 100.0f;
    out[P_ACCEL] = 100.0f;
    out[P_MIRROR] = type < 2 ? 1.0f : 0.0f;
}

void apply_field(Pov *p, int i, float v) {
    switch (i) {
        case P_FOV: p->fov = deg_to_ang(v); break;
        case P_HEIGHT: p->height = v; break;
        case P_LAG: p->lag = v; break;
        case P_ANGLE: p->angle = deg_to_ang(v); break;
        default: break;  // P_SPEED is applied by apply_speed_fov
    }
}

// The value to use for parameter `i` of a view whose own value is `base`. The result stays in the page's range, or
// wherever `base` already is when that lies outside it.
float resolved(const Override &o, int i, float base) {
    if (!o.set[i]) return base;
    if (is_toggle(i)) return o.val[i];
    const float lo = kRange[i].min < base ? kRange[i].min : base;
    const float hi = kRange[i].max > base ? kRange[i].max : base;
    const float v = base + o.val[i];
    return v < lo ? lo : (v > hi ? hi : v);
}

// Whether a setting has an effect in a view. The bumper and hood views (0, 1) sit on the car: they use its own tilt and are not
// smoothed or moved by its acceleration.
bool applies(int type, int param) {
    if (type < 0 || type >= kNumPresets) return false;
    return !(type < 2 && (param == P_TILT || param == P_SMOOTH || param == P_ACCEL));
}

float effective(int type, int i) {
    return g_have_base[type] ? resolved(g_ovr[type], i, g_base[type][i]) : 0.0f;
}

// Like the game's own options: edits take effect at once, but only count as applied when the screen is left with
// Accept. Back asks before discarding (it sees camera_changed()), and a confirmed discard restores g_ovr_open.
bool camera_changed() {
    for (int t = 0; t < kNumPresets; ++t)
        for (int i = 0; i < NPARAM; ++i) {
            if (g_ovr[t].set[i] != g_ovr_open[t].set[i]) return true;
            if (g_ovr[t].set[i] && std::fabs(g_ovr[t].val[i] - g_ovr_open[t].val[i]) > 1e-4f) return true;
        }
    return false;
}

// ---- persistence ----
char g_ini[MAX_PATH];

void ini_path_init() { nfsmw::data_file("nfsmw_camera_settings.ini", g_ini, sizeof(g_ini)); }

void ini_load() {
    int loaded = 0;
    for (int t = 0; t < kNumPresets; ++t) {
        for (int i = 0; i < NPARAM; ++i) {
            char buf[64] = {};
            GetPrivateProfileStringA(kPresetName[t], kKey[i], "", buf, sizeof(buf), g_ini);
            if (buf[0]) {
                g_ovr[t].set[i] = true;
                g_ovr[t].val[i] = static_cast<float>(atof(buf));
                ++loaded;
            }
        }
    }
    log_line("loaded %d setting(s) from %s", loaded, g_ini);

    g_degree_sign = GetPrivateProfileIntA("general", "degree_symbol", 1, g_ini) != 0;

    // A texture's name (hashed like the game: h = h * 33 + c from 0xFFFFFFFF) or its hash as 8 hex digits.
    // Texture names longer than 23 characters are cut short in the game files, so use the hash for those.
    char icon[64] = {};
    GetPrivateProfileStringA("general", "icon", "", icon, sizeof(icon), g_ini);
    char *end = nullptr;
    const unsigned long hex = strtoul(icon, &end, 16);
    if (strlen(icon) == 8 && end && !*end) {
        g_icon = hex;
    } else if (icon[0]) {
        g_icon = texture_hash(icon);
    }
    log_line("icon texture %08x%s%s", g_icon, icon[0] ? " from the ini: " : " (default)", icon);
}

void ini_save() {
    for (int t = 0; t < kNumPresets; ++t) {
        for (int i = 0; i < NPARAM; ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", g_ovr[t].val[i]);
            WritePrivateProfileStringA(kPresetName[t], kKey[i], g_ovr[t].set[i] ? buf : nullptr, g_ini);
        }
    }
}

// ---- Speed FoV: wider FoV and a camera dollied toward the car, growing with the car's speed ----
constexpr unsigned kAnchorVelMag = 0x10;  // CameraAnchor: speed of the car in m/s (written by CameraAnchor::Update)
constexpr float kFullSpeed = 300.0f / 3.6f; // m/s (300 km/h) where the effect reaches its full strength
constexpr float kMaxFov = 140.0f;         // the FoV row's maximum
constexpr float kSpeedFovEase = 0.04f;       // per GetPov call (~110/s): the effect eases in and out over ~0.2 s

struct SpeedState {
    void *anchor;
    float t;  // 0..1 share of full speed, smoothed
};
SpeedState g_speed[2];  // one per player's anchor

void apply_speed_fov(void *anchor, Pov *p, float max_gain_deg) {
    SpeedState *st = nullptr;
    for (SpeedState &s : g_speed)
        if (s.anchor == anchor) st = &s;
    if (!st)
        for (SpeedState &s : g_speed)
            if (!s.anchor) { s.anchor = anchor; st = &s; break; }
    if (!st) return;
    float target = field<float>(anchor, kAnchorVelMag) / kFullSpeed;
    target = target < 0.0f ? 0.0f : (target > 1.0f ? 1.0f : target);
    st->t += (target - st->t) * kSpeedFovEase;
    // Dolly zoom: distance * tan(fov / 2) stays constant, so the car keeps its size on screen while the
    // background widens; the camera moves in by tan(old / 2) / tan(new / 2). Only the square root of that is
    // used: the full amount left the camera so close that braking made it overshoot above the car.
    const float old_deg = ang_to_deg(p->fov);
    float new_deg = old_deg + max_gain_deg * st->t;
    if (new_deg > kMaxFov) new_deg = kMaxFov > old_deg ? kMaxFov : old_deg;
    constexpr float kHalfRad = 3.14159265f / 360.0f;
    if (old_deg > 1.0f) p->lag *= sqrtf(tanf(old_deg * kHalfRad) / tanf(new_deg * kHalfRad));
    p->fov = deg_to_ang(new_deg);
}

// ---- GetPov hook: apply the changes to the preset the game just built ----
using GetPovFn = Pov *(FASTCALL *)(void *, void *, int);
GetPovFn g_orig_getpov = nullptr;

Pov *FASTCALL hook_getpov(void *self, void *edx, int type) {
    Pov *p = g_orig_getpov(self, edx, type);
    if (p && type >= 0 && type < kNumPresets) {
        g_cur_type = type;
        g_anchor = self;
        float now[NPARAM];
        read_base(*p, type, now);
        if (!g_have_base[type] || memcmp(now, g_base[type], sizeof(now)) != 0) {
            static unsigned logged = 0;
            if (logged++ < 200)
                log_line("base %-9s fov=%.1f height=%.3f lag=%.3f pitch=%.1f",
                         kPresetName[type], now[P_FOV], now[P_HEIGHT], now[P_LAG], now[P_ANGLE]);
            memcpy(g_base[type], now, sizeof(now));
            g_have_base[type] = true;
        }
        for (int i = 0; i < NPARAM; ++i)
            if (g_ovr[type].set[i]) apply_field(p, i, resolved(g_ovr[type], i, now[i]));
        if (g_ovr[type].set[P_SPEED]) apply_speed_fov(self, p, resolved(g_ovr[type], P_SPEED, 0.0f));
    }
    return p;
}

// NFSMWExtraOptions (EnableHiddenCameraModes) names the Drift camera with the language string "DRIFT", the only
// all-caps name in the list. The game has its own "Drift" string, translated like the rest, so use that instead.
// Hooked in the game's PODriveCam::Draw, so Options > Player shows it too.
using MethodFn = void(FASTCALL *)(void *, void *);
MethodFn g_orig_drivecam_draw;
void FASTCALL hook_drivecam_draw(void *self, void *edx) {
    g_orig_drivecam_draw(self, edx);
    void *value = field<void *>(self, kRowData);
    if (value && field<unsigned>(value, kLabelHash) == kDriftUpper) game::FEngSetLanguageHash(value, kDrift);
}

// ------------------------------------------------------------------------------------------------
// The Options > Camera page (see custom_menus_api.h). Row 0 is the game's Camera selector; row 1 + p edits parameter p of
// the camera view that selector currently points at.
// ------------------------------------------------------------------------------------------------
int camera_type_being_edited() {
    // The selector row stores the chosen camera in PlayerSettings[player].CurCam (0x14 bytes each from the options
    // settings +0x80); map it to a POV preset.
    unsigned char *settings = static_cast<unsigned char *>(field<void *>(*reinterpret_cast<void **>(game::kFEDatabase), 0x10));
    const int player = *reinterpret_cast<int *>(game::kPlayerToEdit);
    const int cam = *reinterpret_cast<int *>(settings + 0x80 + player * 0x14 + 0x0C);
    const int type = game::GetPOVTypeFromPlayerCamera(cam);
    return (type >= 0 && type < kNumPresets) ? type : g_cur_type;
}

void format_value(char *out, size_t n, int param, float v) {
    const char *deg = g_degree_sign ? "\x01" : " deg";
    switch (param) {
        case P_FOV: snprintf(out, n, "%.0f%s", v, deg); break;
        case P_HEIGHT: snprintf(out, n, "%.2f", v); break;
        case P_LAG: snprintf(out, n, "%.1f", v); break;
        case P_ANGLE: snprintf(out, n, "%.1f%s", v, deg); break;
        case P_SPEED: snprintf(out, n, "%.0f%s", v, deg); break;
        case P_SMOOTH:
        case P_ACCEL:
        case P_TILT: snprintf(out, n, "%.0f%%", v); break;
        default: snprintf(out, n, "%s", v >= 0.5f ? "On" : "Off"); break;
    }
}

// Store the value the player picked for the car being driven, as a difference from that car's own value (unless it
// equals it).
void store_value(int type, int param, float v) {
    const float base = g_have_base[type] ? g_base[type][param] : v;
    if (std::fabs(v - base) < 1e-3f) {
        g_ovr[type].set[param] = false;
    } else {
        g_ovr[type].set[param] = true;
        g_ovr[type].val[param] = is_toggle(param) ? v : std::round((v - base) * 10000.0f) / 10000.0f;
    }
    log_line("edit %s.%s = %g (base %g)%s", kPresetName[type], kKey[param], v, base,
             g_ovr[type].set[param] ? "" : " -> back to game value");
}

opt::Row g_rows[1 + NPARAM];

// The main menu has no car, so its Camera page edits against the hero car's camera: the game's own CameraAnchor, built by its
// constructor for the BMW M3 GTR and used only to read the car's values (never updated or destroyed).
void *hero_anchor() {
    static void *anchor = nullptr;
    if (anchor) return anchor;
    const unsigned char *types = *reinterpret_cast<unsigned char **>(game::kCarTypeInfoArray);
    for (int i = 0; types && i < game::kCarTypeCount; ++i) {
        const unsigned char *type = types + i * game::kCarTypeSize;
        if (_stricmp(reinterpret_cast<const char *>(type), "bmwm3gtr") != 0) continue;
        anchor = game::CameraAnchorCtor(calloc(1, 0x200), field<unsigned>(const_cast<unsigned char *>(type), game::kCarTypeNameHash));
        log_line("main menu: the BMW M3 GTR's camera (anchor %p) stands in for a car", anchor);
        break;
    }
    return anchor;
}

// Where the page reads the game's own values: the car being driven (the options menu is paused over a race), else the hero car.
void *value_source() {
    return game::IsPaused(reinterpret_cast<void *>(game::kGameFlowManager)) ? g_anchor : hero_anchor();
}

bool page_available() { return value_source() != nullptr; }

void page_opened() {
    memcpy(g_ovr_open, g_ovr, sizeof(g_ovr));
    // Read every preset's own values from the game so each can be edited without having been the active view.
    for (int t = 0; t < kNumPresets; ++t) {
        if (Pov *p = g_orig_getpov(value_source(), nullptr, t)) {
            read_base(*p, t, g_base[t]);
            g_have_base[t] = true;
            log_line("game values %-9s fov=%.4f height=%.4f lag=%.4f pitch=%.4f",
                     kPresetName[t], g_base[t][P_FOV], g_base[t][P_HEIGHT], g_base[t][P_LAG], g_base[t][P_ANGLE]);
        }
    }
    log_line("Options > Camera opened (editing %s)", kPresetName[camera_type_being_edited()]);
}

unsigned page_value(int row, char *out, unsigned out_size) {
    if (row < 1) return 0;
    format_value(out, out_size, row - 1, effective(camera_type_being_edited(), row - 1));
    return 0;
}

void page_change(int row, int direction, bool fine) {
    const int param = row - 1;
    if (param < 0 || param >= NPARAM) return;
    const int type = camera_type_being_edited();
    const Range &r = kRange[param];
    float v = effective(type, param);
    if (is_toggle(param)) {
        v = direction > 0 ? 1.0f : 0.0f;
    } else {
        v += direction * (fine ? r.fine : r.step);
        v = std::round(v / r.fine) * r.fine;  // on the fine grid, so the ini gets 2.3 rather than 2.3000002
        v = v < r.min ? r.min : (v > r.max ? r.max : v);
    }
    store_value(type, param, v);
}

// Like the game's own options: edits take effect at once, but only count as applied when the screen is left with
// Accept. Back asks before discarding, and a confirmed discard restores g_ovr_open.
void page_accept() {
    ini_save();
    memcpy(g_ovr_open, g_ovr, sizeof(g_ovr));
    log_line("camera settings applied and saved");
}

void page_discard() { memcpy(g_ovr, g_ovr_open, sizeof(g_ovr)); }

// Rows of settings that do nothing in the selected view are greyed out; row 0 is the Camera selector.
bool page_row_enabled(int row) {
    return row < 1 || row > NPARAM || applies(camera_type_being_edited(), row - 1);
}

// Defaults clears the selected view's changes.
void page_defaults() {
    const int type = camera_type_being_edited();
    memset(&g_ovr[type], 0, sizeof(g_ovr[type]));
    log_line("Defaults: cleared all changes for %s", kPresetName[type]);
}

opt::Page g_page;
char g_pack_path[MAX_PATH];

// The preset a PlayerSettings::CurCam stands for, or -1.
int preset_of(int cam) {
    const int type = game::GetPOVTypeFromPlayerCamera(cam);
    return type >= 0 && type < kNumPresets ? type : -1;
}

}  // namespace

namespace camera_settings {

float smoothing_factor(int type) {
    if (!applies(type, P_SMOOTH) || !g_ovr[type].set[P_SMOOTH]) return 1.0f;
    return resolved(g_ovr[type], P_SMOOTH, 100.0f) / 100.0f;
}

float acceleration_factor(int type) {
    if (!applies(type, P_ACCEL) || !g_ovr[type].set[P_ACCEL]) return 1.0f;
    return resolved(g_ovr[type], P_ACCEL, 100.0f) / 100.0f;
}

float tilt_factor(int type) {
    if (!applies(type, P_TILT) || !g_ovr[type].set[P_TILT]) return 0.0f;
    return resolved(g_ovr[type], P_TILT, 0.0f) / 100.0f;
}

bool in_cycle(int cam) {
    const int type = preset_of(cam);
    return type < 0 || !g_ovr[type].set[P_ENABLED] || g_ovr[type].val[P_ENABLED] >= 0.5f;
}

int mirror_override(int cam) {
    const int type = preset_of(cam);
    if (type < 0 || !g_ovr[type].set[P_MIRROR]) return -1;
    return g_ovr[type].val[P_MIRROR] >= 0.5f ? 1 : 0;
}

}  // namespace camera_settings

// Called by nfsmw_custom_menus when the Options menu first opens.
extern "C" __declspec(dllexport) void nfsmw_custom_menus_provide(const opt::Host *host) {
    g_rows[0] = {opt::kCameraSelectorRow, "", 0, kHelp[NPARAM]};
    for (int p = 0; p < NPARAM; ++p) g_rows[1 + p] = {opt::kValueRow, kLabel[p], 0, kHelp[p]};
    char translations[MAX_PATH];
    nfsmw::data_file("translations\\camera_settings", translations, sizeof(translations));
    host->register_translations(translations);  // before any text is registered
    nfsmw::data_file("nfsmw_camera_settings.tpk", g_pack_path, sizeof(g_pack_path));
    g_page = {"camera", "Camera", "CAMERA", "CAMERA INFO", g_icon, 0, g_pack_path,
              1 + NPARAM, g_rows, page_available, page_opened, page_value, page_change, camera_changed, page_accept,
              page_discard, page_defaults, opt::kNewEntryEverywhere, nullptr, page_row_enabled};
    host->register_page(&g_page);
    camera_settings::previous_camera::provide(host);
}

namespace {

// ------------------------------------------------------------------------------------------------
// Hooks. The required one is checked first: if another mod changed its first bytes, nothing is hooked. Optional
// ones are skipped on their own.
// ------------------------------------------------------------------------------------------------
void install_hooks() {
    const nfsmw::Hook hooks[] = {
        {"getpov", "CameraAnchor::GetPov", 0x4713C0, {0x8B, 0x44, 0x24, 0x04, 0x83, 0xF8, 0x06}, true,
         nfsmw::detour(&hook_getpov), nfsmw::orig(&g_orig_getpov)},
        {"drivecamdraw", "PODriveCam::Draw", 0x51C910, {0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x77, 0x2C}, false,
         nfsmw::detour(&hook_drivecam_draw), nfsmw::orig(&g_orig_drivecam_draw)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), g_log, g_ini);
}

}  // namespace

NFSMW_PLUGIN_MAIN() {
    g_log.open_in_data_folder("nfsmw_camera_settings.log");
    log_line("nfsmw_camera_settings 1.1.0 starting");
    ini_path_init();
    ini_load();
    install_hooks();
    camera_settings::previous_camera::init(g_log, g_ini);
    camera_settings::previous_camera::install_hooks(g_log, g_ini);
    camera_settings::camera_cycle::install_hooks(g_log, g_ini);
    camera_settings::mirror::install_hooks(g_log, g_ini);
    camera_settings::smoothing::install_hooks(g_log, g_ini);
    camera_settings::acceleration::install_hooks(g_log, g_ini);
    camera_settings::tilt::install_hooks(g_log, g_ini);
    return NFSMW_OK;
}

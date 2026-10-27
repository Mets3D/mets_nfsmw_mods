#include <MinHook.h>

#include <algorithm>

#include "device_settings.h"
#include "hooks.h"
#include "input.h"
#include "max_players.h"
#include "extra_device.h"
#include "profile.h"
#include "rumble.h"

namespace pad::rumble {

// One level per effect, 0-100 (per controller: see below): how hard the motors run in an ordinary instance of the effect (a solid
// hit, a normal drift, the moment NOS kicks in; bigger ones stay at that level). The same number means the same strength for
// every effect, and the Settings page plays exactly that when a level changes; 0 is off. The last number of each line is the
// default, the one a controller starts with and Defaults restores. [rumble] enabled is read at start-up. The levels are edited in
// the Settings page (controller_page.cpp) and reloaded whenever the ini changes.
const EffectInfo kEffects[E_COUNT] = {
    {"collisions", "Collisions",
     "Hitting walls, cars and objects: a jolt, harder the more the hit slows your car down (small props barely "
     "register), and a grinding rumble at half strength while sliding along a wall or car.", 100},
    {"landings", "Landings", "The car's body hitting the ground: landing a jump, or bottoming out.", 80},
    {"shifts", "Gear Shifts", "A short kick on every upshift.", 60},
    {"nos", "Nitrous", "A surge when the nitrous kicks in, settling into a steady push while it lasts.", 70},
    {"drift", "Drifting", "The tires sliding sideways. Stronger the faster they slide.", 40},
    {"wheelspin", "Wheelspin", "Spinning wheels (burnouts, hard launches) and locked brakes.", 40},
    {"rev_limiter", "Rev Limiter",
     "Full throttle at maximum revs: at top speed in top gear, or a manual gear change left too late.", 40},
    {"road", "Rough Ground",
     "Grass, gravel, dirt and wooden bridges, stronger with speed. Asphalt and concrete are smooth.", 40},
    {"engine", "Engine", "A hum that rises with revs and throttle, all the time while driving.", 0},
    {"tire_blowout", "Tire Blowouts", "Police spike strips: a bang, then the flat tire thumping as it turns.", 80},
    {"engine_blown", "Blown Engine",
     "Drag races: the engine blowing from overheating. Also the sabotaged car in the opening race.", 80},
};
// Each XInput controller (0-3) has its own levels, kept in [rumble_1] .. [rumble_4]; an effect a controller has no key for
// takes the default above.
constexpr int kSlots = 4;
bool g_rumble_enabled = true;
float g_levels[kSlots][E_COUNT];  // 0..1
bool g_vibration[kSlots] = {true, true, true, true};  // rumble on, per controller
int g_edit_slot = 0;              // the controller the Settings page edits
unsigned long long g_ini_stamp = 0;  // profile::stamp() when the levels were read or written

// A controller's levels are in [rumble_<id>] of the profile's controller_settings.ini; a key missing there takes the same section of the
// mod's ini, then the default.
void section_of(int slot, char *out, size_t n) { snprintf(out, n, "rumble_%s", device_settings::key_of(slot)); }

void load_slot_levels(int slot) {
    char section[64];
    section_of(slot, section, sizeof(section));
    g_vibration[slot] = profile::get_int(section, "vibration", 1) != 0;
    for (int e = 0; e < E_COUNT; ++e)
        g_levels[slot][e] = clamp01(profile::get_float(section, kEffects[e].key, static_cast<float>(kEffects[e].def)) / 100.0f);
}

void load_rumble_levels() {
    char text[512];
    int n = snprintf(text, sizeof(text), "rumble: enabled=%d, controller 1:", g_rumble_enabled);
    for (int slot = 0; slot < kSlots; ++slot) load_slot_levels(slot);
    for (int e = 0; e < E_COUNT; ++e) n += snprintf(text + n, sizeof(text) - n, " %s=%.0f", kEffects[e].key, g_levels[0][e] * 100.0f);
    g_ini_stamp = profile::stamp();
    log_line("%s", text);
}

void save_rumble_levels(int slot) {
    char section[64];
    section_of(slot, section, sizeof(section));
    profile::set(section, "vibration", g_vibration[slot] ? "1" : "0");
    for (int e = 0; e < E_COUNT; ++e) {
        char value[16];
        snprintf(value, sizeof(value), "%d", static_cast<int>(lroundf(g_levels[slot][e] * 100.0f)));
        profile::set(section, kEffects[e].key, value);
    }
    g_ini_stamp = profile::stamp();
    log_line("rumble: levels saved");
}

// Called every frame of a race: picks up edits to the ini within half a second.
void reload_rumble_levels_if_changed() {
    static DWORD next_check = 0;
    if (GetTickCount() < next_check) return;
    next_check = GetTickCount() + 500;
    if (profile::stamp() != g_ini_stamp) load_rumble_levels();
}

bool enabled() { return g_rumble_enabled && xinput::can_vibrate(); }

void load_settings() {
    g_rumble_enabled = GetPrivateProfileIntA("rumble", "enabled", 1, g_ini) != 0;
    load_rumble_levels();
}

int clamp_slot(int slot) { return slot >= 0 && slot < kSlots ? slot : 0; }
float level(int slot, int effect) { return g_levels[clamp_slot(slot)][effect]; }
void set_level(int slot, int effect, float value) { g_levels[clamp_slot(slot)][effect] = clamp01(value); }
bool vibration(int slot) { return g_vibration[clamp_slot(slot)]; }
void set_vibration(int slot, bool on) { g_vibration[clamp_slot(slot)] = on; }
int edit_slot() { return g_edit_slot; }
void set_edit_slot(int slot) { g_edit_slot = clamp_slot(slot); }
void save_levels(int slot) { save_rumble_levels(clamp_slot(slot)); }
void reload_slot(int slot) { load_slot_levels(clamp_slot(slot)); }

// ------------------------------------------------------------------------------------------------
// Rumble. The game drives force feedback through an IFeedback object that LocalPlayer::SetControllerPort takes from
// the player's input device. LocalPlayer::DoFFB feeds it every frame (per wheel: tire slip, tire skid and road noise
// with the wheel's surface; then engine revs, shift potential, NOS, and a blown engine or a gear change), and
// LocalPlayer::OnCollision reports every collision. On PC the device's object (inside GameDevice) does nothing, so a
// controller never rumbles. SetControllerPort is hooked to put our object in front of it: every call is passed on to
// the game's object, and the events become XInput motor levels. The game's own conditions still apply: with the
// profile's Rumble setting off, while paused, under AI control or during animations it only calls PauseEffects.
// IFeedback methods are thiscall (this in ECX, arguments on the stack, callee cleans up), which fastcall functions
// with an unused EDX argument match; bools arrive as 32-bit values of which only the low byte is set.
// ------------------------------------------------------------------------------------------------
// A surface (SimSurface: an Attrib instance with its layout data at +8).
struct SurfaceData {
    unsigned key;  // the collection's key (+4: collection, its key at +0x20)
    const char *name;
    float noise_freq, noise_amp, noise_min, noise_max;  // RenderNoise: the body shake the renderer adds, in degrees
    unsigned wheel_freq, wheel_intensity;                // WheelEffectFrequency / WheelEffectIntensity
};

bool read_surface(const void *surface, SurfaceData *out) {
    const unsigned char *layout =
        surface ? *reinterpret_cast<const unsigned char *const *>(static_cast<const char *>(surface) + 8) : nullptr;
    if (!layout) return false;
    const float *noise = reinterpret_cast<const float *>(layout + 0xCC);
    const unsigned char *collection = *reinterpret_cast<const unsigned char *const *>(static_cast<const char *>(surface) + 4);
    *out = {collection ? *reinterpret_cast<const unsigned *>(collection + 0x20) : 0u,
            *reinterpret_cast<const char *const *>(layout + 0xDC), noise[0], noise[1], noise[2], noise[3],
            *reinterpret_cast<const unsigned short *>(layout + 0xF8), layout[0xFA]};
    return true;
}

// Wheels in the air get the "null" surface, wheels with a blown tire this one (LocalPlayer::DoFFB).
constexpr unsigned kBlownTireSurface = 0xD929E923;
// Wheels in the air get the "null" surface.
bool on_ground(const SurfaceData &d) { return d.name && _stricmp(d.name, "null") != 0; }
bool on_ground(const void *surface) {
    SurfaceData d;
    return read_surface(surface, &d) && on_ground(d);
}

// Motors. Both run at the same level: an effect's level is simply how hard the controller shakes. Only the rumble
// thread talks to the controller: in a race the game thread just sets the level it should play (each frame), and
// the Settings page's previews are played by the thread itself, so a crash and its preview reach the controller the same
// way. The thread also stops the motors when the game stops updating (loading, a hang).
// Every player can rumble (the others read the extra devices, see extra_device.h); g_cur is the one the game thread is
// working on, set at every entry from the game (the feedback objects know their player).
constexpr int kPlayers = nfsmw::kMaxPlayers;
int g_cur = 0;
volatile float g_game_level[kPlayers] = {};
volatile DWORD g_last_rumble_update[kPlayers] = {};  // when the race last set g_game_level
volatile LONG g_preview_effect = -1;      // the effect being previewed, or -1
volatile DWORD g_preview_start = 0;

volatile LONG g_stop_now[kPlayers] = {};  // pause / reset: the motors stop at once, not after the short hold below

void set_motors(float level) { g_game_level[g_cur] = level; }

// The thread's side. A change is sent at most every 30 ms (a rise of a tenth or more, or stopping, at once); a steady
// level is sent again every half second, as Xbox controllers on Bluetooth stop a rumble after a while.
void send_level(int player, float level, int port) {
    static WORD sent[kPlayers] = {};
    static DWORD last_send[kPlayers] = {};
    static int sent_port[kPlayers] = {};
    static bool sent_port_set = false;
    if (!sent_port_set) {
        sent_port_set = true;
        std::fill(sent_port, sent_port + kPlayers, -1);
    }
    if (!xinput::can_vibrate()) return;
    if (sent_port[player] != port) {  // the player changed controller: the old one stops
        if (sent_port[player] >= 0 && sent[player]) {
            xinput::Vibration off = {0, 0};
            xinput::set_state(sent_port[player], &off);
        }
        sent_port[player] = port;
        sent[player] = 0;
    }
    if (port < 0) return;
    const WORD v = static_cast<WORD>(clamp01(level) * 65535.0f + 0.5f);
    const DWORD now = GetTickCount();
    const bool changed = abs(v - sent[player]) >= 1024 || (v == 0) != (sent[player] == 0);
    const bool urgent = v == 0 || v >= sent[player] + 6554;
    if (!(changed && (urgent || now - last_send[player] >= 30)) && !(v && now - last_send[player] >= 500)) return;
    xinput::Vibration vib = {v, v};
    const DWORD result = xinput::set_state(port, &vib);
    static int failures = 0;
    if (result != ERROR_SUCCESS && failures++ < 5) log_line("rumble: XInputSetState(%d) failed: %lu", port, result);
    sent[player] = v;
    last_send[player] = now;
}

// Effect shapes, 0..1, shared by the game and the previews. A pulse goes smoothly between floor and 1: the motors take
// tens of milliseconds to spin up and Bluetooth adds latency, so on/off chatter faster than a few Hz comes out weak.
float pulse_at(float phase, float floor) { return floor + (1.0f - floor) * (0.5f + 0.5f * std::sin(6.2831853f * phase)); }
// A jolt (crash, landing, blowout): full strength for a moment, then a fade, both longer the harder the hit.
float jolt_hold(float severity) { return 0.05f + 0.15f * severity; }
float jolt_fade(float severity) { return 0.08f + 0.25f * severity; }
float nos_shape(float t) { return 0.85f + 0.15f * std::exp(-t / 0.6f); }  // a surge, then a steady push
// Measured while driving: tire skid (sideways, m/s) is 2-5 in ordinary corners, 7-11 in drifts; wheelspin (m/s)
// 4-10 at a launch, 16-30 in burnouts and with locked brakes; a collision's speed change (m/s) 0.5-1 for props, 2-6 for
// ordinary hits, 7-17 for big crashes.
float drift_shape(float skid) { return ramp(skid, 4.5f, 9.0f); }
float wheelspin_shape(float slip) { return ramp(slip, 5.0f, 12.0f); }
float collision_severity(float speed_change) { return std::pow(ramp(speed_change, 0.8f, 6.0f), 0.6f); }
float shift_shape(float t) { return std::exp(-t / 0.07f); }
float either(float a, float b) { return 1.0f - (1.0f - a) * (1.0f - b); }  // two shakes at once

// A sample of an effect at full strength, t seconds in; false when it is over.
bool preview_shape(int effect, float t, float *out) {
    const auto jolt = [t](float severity) {
        return t < jolt_hold(severity) ? severity : severity * std::exp(-(t - jolt_hold(severity)) / jolt_fade(severity));
    };
    float s = 1.0f, length = 0.8f;
    switch (effect) {
        case E_COLLISIONS:
        case E_LANDINGS: s = jolt(1.0f); length = 1.0f; break;
        case E_SHIFTS: s = shift_shape(t); length = 0.35f; break;
        case E_NOS: s = nos_shape(t); length = 1.5f; break;
        case E_REV_LIMITER: s = pulse_at(t * 8.0f, 0.4f); length = 1.0f; break;
        case E_ROAD: s = pulse_at(t * 6.0f, 0.6f); length = 1.0f; break;
        case E_TIRE_BLOWOUT: s = either(jolt(1.0f), 0.25f + 0.75f * pulse_at(t * 5.0f, 0.0f)); length = 1.5f; break;
        case E_ENGINE_BLOWN: s = either(jolt(1.0f), pulse_at(t * 2.5f, 0.2f)); length = 1.6f; break;
        default: break;  // drifting, wheelspin, engine: steady
    }
    *out = s;
    return t < length;
}

// Which controller a player's rumble goes to: player 1 the one in use (or none, when no controller reaches the game),
// the others the one that is the source of their device (-1: none, or the keyboard).
int player_slot(int player) {
    if (player >= 1) return extra_device::source_slot_of(player - 1);
    return input::player_one_rumble_slot();
}

// The levels of the controller a player rumbles on (the first controller's, when none does).
const float *levels_of(int player) { return g_levels[clamp_slot(player_slot(player))]; }

DWORD WINAPI rumble_thread(void *) {
    for (;;) {
        // The times come from the other thread: read them before GetTickCount(), and compare signed, so a time set a
        // tick later than our clock reading never looks like one from 49 days ago. (It did once: the motors were cut
        // to 0 every few frames, which made every effect in a race feel weak.)
        for (int player = 0; player < kPlayers; ++player) {
            float level = 0.0f;
            const LONG effect = player == 0 ? g_preview_effect : -1;  // the Settings page plays on player 1's controller
            const DWORD preview_start = g_preview_start, last_update = g_last_rumble_update[player];
            const LONG preview_age = static_cast<LONG>(GetTickCount() - preview_start);
            const LONG update_age = static_cast<LONG>(GetTickCount() - last_update);
            if (effect >= 0 && effect < E_COUNT) {
                float s = 0.0f;
                if (preview_shape(effect, (preview_age > 0 ? preview_age : 0) / 1000.0f, &s))
                    level = s * g_levels[g_edit_slot][effect];
                else InterlockedCompareExchange(&g_preview_effect, -1, effect);
            } else if (update_age <= 250) {
                level = g_game_level[player];
            }
            if (InterlockedExchange(&g_stop_now[player], 0) || (effect < 0 && update_age > 250)) level = 0.0f;
            send_level(player, level, effect >= 0 ? g_edit_slot : player_slot(player));  // previews play on the edited controller
        }
        Sleep(10);
    }
}

// Rumble only while the controller is being used, not while it lies on the desk and someone plays on the keyboard.
bool pad_in_use() {
    static bool ever_used = false;
    static DWORD last_active = 0;
    xinput::Gamepad pad;
    if (!xinput::read(&pad)) return false;
    const DWORD now = GetTickCount();
    const auto moved = [](SHORT v) { return v > 9000 || v < -9000; };
    if (pad.buttons || pad.left_trigger > 40 || pad.right_trigger > 40 || moved(pad.lx) || moved(pad.ly) ||
        moved(pad.rx) || moved(pad.ry)) {
        ever_used = true;
        last_active = now;
    }
    return ever_used && now - last_active < 30000;
}

bool game_in_foreground() {
    DWORD pid = 0;
    if (HWND w = GetForegroundWindow()) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

struct RumbleState {
    // This frame (cleared by BeginUpdate).
    float road;                 // the roughest wheel's body shake, degrees
    float road_freq;            // its frequency, Hz
    float speed;                // m/s
    float skid, slip;           // the largest of the wheels on the ground, m/s
    float powerband, throttle;  // 0..1
    bool nos, shifting, blown;
    int flat_tires;
    // Read from the car's transmission before each DoFFB (the game only reports gear changes for cars without engine
    // damage, which the player's car has). -1 = unknown.
    int gear;
    // Carried over.
    bool was_nos, was_shifting, was_blown;
    int was_flat_tires, was_gear;
    float time, road_phase, tire_phase, nos_time, redline_time, shift_time;
    float jolt, jolt_hold, jolt_fade;  // crash: level, seconds left at full strength, fade time
    float scrape, scrape_hold;         // sliding along a wall or a car
    int jolt_effect, scrape_effect;    // the effects they belong to
    LARGE_INTEGER last;                // the previous EndUpdate, 0 after a pause
} g_states[kPlayers];

RumbleState &rs() { return g_states[g_cur]; }

void clear_rumble_effects() {
    rs().was_nos = rs().was_shifting = rs().was_blown = false;
    rs().was_flat_tires = 0;
    rs().was_gear = -1;
    rs().jolt = rs().jolt_hold = rs().scrape = rs().scrape_hold = rs().redline_time = 0.0f;
    rs().shift_time = 1.0f;
    rs().last.QuadPart = 0;
}

// A jolt (crash, landing, blowout) of an effect; severity 0..1 is how hard the hit was.
void start_jolt(int effect, float severity) {
    const float v = levels_of(g_cur)[effect] * severity;
    if (v <= rs().jolt) return;
    rs().jolt = v;
    rs().jolt_effect = effect;
    rs().jolt_hold = jolt_hold(severity);
    rs().jolt_fade = jolt_fade(severity);
}

// Scraping along something (strength 0..1): a grinding rumble at half the effect's level, while it lasts.
void start_scrape(int effect, float strength) {
    const float v = levels_of(g_cur)[effect] * 0.5f * strength;
    rs().scrape_hold = 0.08f;
    if (v <= rs().scrape) return;
    rs().scrape = v;
    rs().scrape_effect = effect;
}

// The player's Vibration setting, read before each DoFFB (see hook_do_ffb).

void update_rumble() {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = 1.0f / 60.0f;
    if (rs().last.QuadPart)
        dt = static_cast<float>(now.QuadPart - rs().last.QuadPart) / static_cast<float>(freq.QuadPart);
    if (dt > 0.1f) dt = 0.1f;
    rs().last = now;
    rs().time += dt;
    g_last_rumble_update[g_cur] = GetTickCount();
    reload_rumble_levels_if_changed();
    const float *level = levels_of(g_cur);

    // Each effect gives its level times how strong it is right now (0..1, reaching 1 in an ordinary instance of the
    // event: a normal drift, a solid hit). Effects at the same time combine like independent shakes, 1 - (1 - a)(1 - b),
    // so together they never go past full.
    float out = 0.0f;
    const auto add = [&](int effect, float v) { out = either(out, clamp01(v)); };

    // Rough ground: the roughest wheel, pulsing faster with speed.
    if (rs().road > 0.0f) {
        rs().road_phase = std::fmod(rs().road_phase + rs().road_freq * dt, 1.0f);
        add(E_ROAD, level[E_ROAD] * rs().road * pulse_at(rs().road_phase, 0.6f));
    }
    add(E_DRIFT, level[E_DRIFT] * drift_shape(rs().skid));
    add(E_WHEELSPIN, level[E_WHEELSPIN] * wheelspin_shape(rs().slip));
    const float pb = rs().powerband;
    add(E_ENGINE, level[E_ENGINE] * pb * pb * (0.3f + 0.7f * rs().throttle));
    // The rev limiter: held at maximum revs for a while (an automatic gearbox sits there briefly before each upshift).
    rs().redline_time = pb > 0.985f && rs().throttle > 0.6f ? rs().redline_time + dt : 0.0f;
    if (rs().redline_time > 0.6f) add(E_REV_LIMITER, level[E_REV_LIMITER] * pulse_at(rs().time * 8.0f, 0.4f));
    if (rs().blown) {
        if (!rs().was_blown) start_jolt(E_ENGINE_BLOWN, 1.0f);
        add(E_ENGINE_BLOWN, level[E_ENGINE_BLOWN] * pulse_at(rs().time * 2.5f, 0.2f));
    }
    rs().was_blown = rs().blown;
    // Tire blowouts: a bang, then the flat tire thumping once per turn of the wheel (about 2 m).
    if (rs().flat_tires > rs().was_flat_tires) start_jolt(E_TIRE_BLOWOUT, 1.0f);
    rs().was_flat_tires = rs().flat_tires;
    if (rs().flat_tires > 0 && rs().speed > 1.0f) {
        const float rate = rs().speed / 2.0f < 8.0f ? rs().speed / 2.0f : 8.0f;
        rs().tire_phase = std::fmod(rs().tire_phase + rate * dt, 1.0f);
        add(E_TIRE_BLOWOUT, level[E_TIRE_BLOWOUT] * ramp(rs().speed, 1.0f, 10.0f) *
                                (0.25f + 0.75f * pulse_at(rs().tire_phase, 0.0f)));
    }
    if (rs().nos) {
        if (!rs().was_nos) rs().nos_time = 0.0f;
        rs().nos_time += dt;
        add(E_NOS, level[E_NOS] * nos_shape(rs().nos_time));
    }
    rs().was_nos = rs().nos;
    // Upshifts, from the transmission's gear (forward gears only); the game's own report (which also includes
    // downshifts) only when the gear is unknown.
    const bool upshift = rs().gear >= 2 && rs().was_gear >= 2 && rs().gear > rs().was_gear;
    if (upshift || (rs().gear < 0 && rs().shifting && !rs().was_shifting)) rs().shift_time = 0.0f;
    if (rs().gear >= 0) rs().was_gear = rs().gear;
    rs().was_shifting = rs().shifting;
    if (rs().shift_time < 0.5f) {
        add(E_SHIFTS, level[E_SHIFTS] * shift_shape(rs().shift_time));
        rs().shift_time += dt;
    }
    // Jolts (collisions, landings, blowouts) and scraping: started by events, carrying their own level.
    if (rs().jolt > 0.0f) {
        add(rs().jolt_effect, rs().jolt);
        if (rs().jolt_hold > 0.0f) rs().jolt_hold -= dt;
        else rs().jolt *= std::exp(-dt / rs().jolt_fade);
        if (rs().jolt < 0.02f) rs().jolt = 0.0f;
    }
    if (rs().scrape > 0.0f) {
        add(rs().scrape_effect, rs().scrape * pulse_at(rs().time * 10.0f, 0.5f));
        if (rs().scrape_hold > 0.0f) rs().scrape_hold -= dt;
        else rs().scrape *= std::exp(-dt / 0.05f);
        if (rs().scrape < 0.02f) rs().scrape = 0.0f;
    }

    // The controller's Vibration setting off, an idle controller or the game in the background: no rumble.
    if (!vibration(player_slot(g_cur)) || (g_cur == 0 && !pad_in_use()) || !game_in_foreground()) out = 0.0f;
    set_motors(out);
}

struct Feedback {
    void **vtbl;
    void *inner;  // the game's object, which every call is passed on to
    int index;    // the player: 0 player 1, 1 the second player, 2 the third ...
} g_feedbacks[kPlayers];

struct FeedbackInit {
    FeedbackInit() {
        for (int i = 0; i < kPlayers; ++i) g_feedbacks[i] = {nullptr, nullptr, i};
    }
} g_feedback_init;

template <typename... A>
void pass_on(int index, A... args) {
    void *inner = g_feedbacks[g_cur].inner;
    if (!inner) return;
    using Fn = void(FASTCALL *)(void *, void *, A...);
    reinterpret_cast<Fn>((*static_cast<void ***>(inner))[index])(inner, nullptr, args...);
}

// Slot 0, the deleting destructor: never called, the game does not own this object.
void *FASTCALL fb_destroy(Feedback *self, void *, int) { return self; }
void FASTCALL fb_pause(Feedback *self, void *) { g_cur = self->index;
    pass_on(1);
    clear_rumble_effects();
    set_motors(0.0f);
    g_stop_now[g_cur] = 1;
}
void FASTCALL fb_resume(Feedback *self, void *) { g_cur = self->index; pass_on(2); }
void FASTCALL fb_reset(Feedback *self, void *) { g_cur = self->index;
    pass_on(3);
    clear_rumble_effects();
    set_motors(0.0f);
    g_stop_now[g_cur] = 1;
}
void FASTCALL fb_begin(Feedback *self, void *) { g_cur = self->index;
    pass_on(4);
    rs().road = rs().road_freq = rs().speed = rs().skid = rs().slip = rs().powerband = rs().throttle = 0.0f;
    rs().nos = rs().shifting = rs().blown = false;
    rs().flat_tires = 0;
}
void FASTCALL fb_end(Feedback *self, void *) { g_cur = self->index;
    pass_on(5);
    update_rumble();
}
void FASTCALL fb_road_noise(Feedback *self, void *, int front, const void *surface, float speed) { g_cur = self->index;
    pass_on(6, front, surface, speed);
    rs().speed = speed;
    SurfaceData d;
    if (!read_surface(surface, &d)) return;
    if (d.key == kBlownTireSurface) ++rs().flat_tires;
    if (!on_ground(d)) return;
    // How rough: the renderer's body shake in degrees (wood, grass and gravel: 1), or for surfaces without one (the
    // railroad) the console rumble intensity, about 100 for rough ground. Full from 15 m/s, pulsing faster with speed.
    const float shake = d.noise_freq * d.noise_amp * d.noise_max > 0.0f ? d.noise_amp : 0.0f;
    const float k = ramp(speed, 0.0f, 15.0f);
    const float rough = clamp01((shake > d.wheel_intensity / 110.0f ? shake : d.wheel_intensity / 110.0f) * k);
    if (rough > rs().road) {
        rs().road = rough;
        rs().road_freq = (d.noise_freq > 0.0f ? d.noise_freq : 8.0f) * (k > 0.3f ? k : 0.3f);
    }
}
void FASTCALL fb_tire_skid(Feedback *self, void *, int front, const void *surface, float skid) { g_cur = self->index;
    pass_on(7, front, surface, skid);
    if (skid > rs().skid && on_ground(surface)) rs().skid = skid;
}
void FASTCALL fb_tire_slip(Feedback *self, void *, int front, const void *surface, float slip) { g_cur = self->index;
    pass_on(8, front, surface, slip);
    if (slip > rs().slip && on_ground(surface)) rs().slip = slip;
}
void FASTCALL fb_rpm(Feedback *self, void *, float powerband, float overrev, float throttle) { g_cur = self->index;
    pass_on(9, powerband, overrev, throttle);
    rs().powerband = clamp01(powerband);
    rs().throttle = clamp01(throttle);
}
void FASTCALL fb_shift_potential(Feedback *self, void *, int potential) { g_cur = self->index; pass_on(10, potential); }
void FASTCALL fb_nos(Feedback *self, void *, int engaged, float capacity) { g_cur = self->index;
    pass_on(11, engaged, capacity);
    rs().nos = (engaged & 0xFF) != 0;
}
void FASTCALL fb_engine_blown(Feedback *self, void *, int blown) { g_cur = self->index;
    pass_on(12, blown);
    rs().blown = (blown & 0xFF) != 0;
}
void FASTCALL fb_shifting(Feedback *self, void *, int shifting) { g_cur = self->index;
    pass_on(13, shifting);
    rs().shifting = (shifting & 0xFF) != 0;
}

// COLLISION_INFO (Sim::Collision::Info): bits at +0x1C (type in bits 0-2: 1 object, 2 world, 3 ground; sliding in
// bit 7), closing velocity +0x20, force +0x2C, impulseA +0x5C and impulseB +0x6C (each body's change of speed, m/s),
// sliding velocity +0x70.
void FASTCALL fb_collision(Feedback *self, void *, const unsigned char *info, int iam_a) { g_cur = self->index;
    pass_on(14, info, iam_a);
    if (!info) return;
    const unsigned bits = *reinterpret_cast<const unsigned *>(info + 0x1C);
    const float *sliding_vel = reinterpret_cast<const float *>(info + 0x70);
    const float dv = std::fabs(*reinterpret_cast<const float *>(info + ((iam_a & 0xFF) ? 0x5C : 0x6C)));
    const float sliding_speed = std::sqrt(sliding_vel[0] * sliding_vel[0] + sliding_vel[1] * sliding_vel[1] +
                                          sliding_vel[2] * sliding_vel[2]);
    const bool sliding = (bits >> 7) & 1;
    const bool ground = (bits & 7) == 3;
    const float severity = collision_severity(dv);
    const int effect = ground ? E_LANDINGS : E_COLLISIONS;
    if (severity > 0.0f) start_jolt(effect, severity);
    if (sliding) start_scrape(effect, ramp(sliding_speed, 2.0f, 12.0f));
}

void *g_feedback_vtbl[15] = {
    reinterpret_cast<void *>(&fb_destroy),         reinterpret_cast<void *>(&fb_pause),
    reinterpret_cast<void *>(&fb_resume),          reinterpret_cast<void *>(&fb_reset),
    reinterpret_cast<void *>(&fb_begin),           reinterpret_cast<void *>(&fb_end),
    reinterpret_cast<void *>(&fb_road_noise),      reinterpret_cast<void *>(&fb_tire_skid),
    reinterpret_cast<void *>(&fb_tire_slip),       reinterpret_cast<void *>(&fb_rpm),
    reinterpret_cast<void *>(&fb_shift_potential), reinterpret_cast<void *>(&fb_nos),
    reinterpret_cast<void *>(&fb_engine_blown),    reinterpret_cast<void *>(&fb_shifting),
    reinterpret_cast<void *>(&fb_collision),
};

// LocalPlayer::DoFFB (this view: feedback object +0x58, steering wheel +0x5C). Any connected DirectInput joystick
// counts as a force feedback wheel (SteeringWheelDevice::IsConnected only asks whether the port has one), and then
// DoFFB hands the frame to the wheel's UpdateForces (DirectInput effects, which an Xbox controller doesn't have)
// instead of the feedback object. With only XInput controllers connected, the wheel is hidden from DoFFB for the call.
// It stays everywhere else: the steering model asks the same device for its steering type.

// UTL::COM: an interface's object (+4) answers QueryInterface(handle) (0x5D59F0, thiscall, ret 4) with its other
// interfaces; a handle is the address of the interface's _IHandle function.
constexpr uintptr_t kQueryInterface = 0x5D59F0, kITransmission = 0x404010;
void *query_interface(void *iface, uintptr_t handle) {
    void *object = iface ? field<void *>(iface, 4) : nullptr;
    if (!object) return nullptr;
    return reinterpret_cast<void *(FASTCALL *)(void *, void *, uintptr_t)>(kQueryInterface)(object, nullptr, handle);
}

// A virtual method without arguments; the result as a 32-bit value (bools: low byte only).
unsigned call_method(void *object, int index) {
    using Fn = unsigned(FASTCALL *)(void *, void *);
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[index])(object, nullptr);
}

// The player's gear, from the car's ITransmission (GetGear is method 1: 0 reverse, 1 neutral, 2 first, ...). The
// player's ISimable comes from its IEntity at +0x2C in this view (GetSimable, method 3), as DoFFB does it.
int player_gear(void *player) {
    void *simable = reinterpret_cast<void *>(call_method(static_cast<char *>(player) + 0x2C, 3));
    void *transmission = query_interface(simable, kITransmission);
    return transmission ? static_cast<int>(call_method(transmission, 1)) : -1;
}

// The game's own Vibration setting, the player's: PlayerSettings (LocalPlayer::GetSettings 0x6E99A0, this = SetControllerPort's view) +0x4.
// Only LocalPlayer::OnCollision looks at it (the PC build's CanDoFFB fetches the settings but never checks the flag), and there is no way to
// change it in the PC game's menus; the controller's own setting (device_settings, the Settings page) is what counts, so the game's is put on
// whenever a player gets their controller (SetControllerPort: each race, after the profile's settings were loaded) to let the collisions through.
constexpr uintptr_t kGetSettings = 0x6E99A0;
void keep_game_vibration_on(void *view) {
    const auto get = reinterpret_cast<unsigned char *(FASTCALL *)(void *, void *)>(kGetSettings);
    if (unsigned char *settings = get(view, nullptr)) settings[0x4] = 1;
}

// Our feedback object in front of the player's (`ffb` is the player's field for it): the first for the player with a game
// device, the second for a player without one (port -1: no device, so the game has no object for them) when a second
// player has been given a controller. Returns the object's index, or -1 when the player rumbles through the game's
// own object.
int install_feedback(void *&ffb, int port) {
    for (int i = 0; i < kPlayers; ++i)
        if (ffb == &g_feedbacks[i]) return i;
    if (!g_rumble_enabled || !xinput::can_vibrate()) return -1;
    const int slot = extra_device::slot_of_port(port);
    const int index = port < 0 ? -1 : slot >= 0 ? slot + 1 : 0;
    if (index < 0) return -1;
    Feedback &feedback = g_feedbacks[index];
    feedback.vtbl = g_feedback_vtbl;
    feedback.inner = ffb;
    ffb = &feedback;
    g_cur = index;
    clear_rumble_effects();
    log_line("rumble: player %d (port %d) now rumbles through mets_controller_support (the game's feedback object: %p)", index + 1, port, feedback.inner);
    return index;
}

using DoFfbFn = void(FASTCALL *)(void *, void *);
DoFfbFn g_orig_do_ffb;
void FASTCALL hook_do_ffb(void *player, void *edx) {
    // DoFFB's view of the player has the feedback object at +0x58 and the steering wheel at +0x5C; SetControllerPort's
    // view (0x4C further on) has the object at +0xC.
    void *&ffb = field<void *>(player, 0x58);
    int index = -1;
    for (int i = 0; i < kPlayers; ++i)
        if (ffb == &g_feedbacks[i]) index = i;
    if (index >= 0) {
        g_cur = index;
        rs().gear = player_gear(player);
    }
    void *&wheel = field<void *>(player, 0x5C);
    void *const saved = wheel;
    const bool hide = saved && index >= 0 && input::only_xinput_controllers();
    static bool logged = false;
    if (hide && !logged) {
        logged = true;
        log_line("rumble: the game's DirectInput wheel force feedback is skipped for the XInput controller");
    }
    if (hide) wheel = nullptr;
    g_orig_do_ffb(player, edx);
    if (hide) wheel = saved;
}

// LocalPlayer::SetControllerPort(port): this view has the feedback object at +0xC. The game's object (the
// device's) is only replaced when the port changes, so ours stays in front until then.
using SetPortFn = void(FASTCALL *)(void *, void *, int);
SetPortFn g_orig_set_controller_port;
void FASTCALL hook_set_controller_port(void *player, void *edx, int port) {
    port = extra_device::port_for_player(port);  // the second player reads the extra device
    g_orig_set_controller_port(player, edx, port);
    keep_game_vibration_on(player);
    install_feedback(field<void *>(player, 0x0C), port);
}

void play_preview(int effect) {
    g_preview_start = GetTickCount();
    g_preview_effect = effect;
}

void start() {
    if (enabled()) CloseHandle(CreateThread(nullptr, 0, rumble_thread, nullptr, 0, nullptr));
}

void install_hooks() {
    if (!enabled()) return;
    const nfsmw::Hook hooks[] = {
        {"set_port", "LocalPlayer::SetControllerPort", 0x6F22E0, {0x8B, 0x44, 0x24, 0x04, 0x57, 0x8B, 0xF9}, false,
         nfsmw::detour(&hook_set_controller_port), nfsmw::orig(&g_orig_set_controller_port)},
        {"do_ffb", "LocalPlayer::DoFFB", 0x6EE490, {0x6A, 0xFF, 0x68, 0x71, 0xE9, 0x87, 0x00}, false,
         nfsmw::detour(&hook_do_ffb), nfsmw::orig(&g_orig_do_ffb)},
    };
    nfsmw::install_hooks(hooks, 2, g_log, nullptr, "rumble");
}

DWORD WINAPI pulse_thread(LPVOID arg) {
    const int port = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    xinput::Vibration on = {30000, 30000}, off = {0, 0};
    xinput::set_state(port, &on);
    Sleep(300);
    xinput::set_state(port, &off);
    return 0;
}

void pulse(int slot) {
    if (slot < 0 || slot > 3 || !xinput::set_state) return;
    HANDLE thread = CreateThread(nullptr, 0, pulse_thread, reinterpret_cast<LPVOID>(static_cast<intptr_t>(slot)), 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace pad::rumble

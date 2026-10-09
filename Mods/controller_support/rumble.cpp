#include <MinHook.h>

#include "hooks.h"
#include "input.h"
#include "rumble.h"

namespace pad::rumble {

// [rumble]: one level per effect, 0-100: how hard the motors run in an ordinary instance of the effect (a solid hit, a
// normal drift, the moment NOS kicks in; bigger ones stay at that level). The same number means the same strength for
// every effect, and Options > Rumble plays exactly that when a level changes; 0 is off.
// enabled is read at start-up. The levels are also edited in Options > Rumble (rumble_page.cpp), and reloaded whenever
// the ini changes.
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
bool g_rumble_enabled = true;
float g_level[E_COUNT];  // 0..1
FILETIME g_ini_time{};

FILETIME ini_write_time() {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    GetFileAttributesExA(g_ini, GetFileExInfoStandard, &a);
    return a.ftLastWriteTime;
}

void load_rumble_levels() {
    char text[512];
    int n = snprintf(text, sizeof(text), "rumble: enabled=%d", g_rumble_enabled);
    for (int e = 0; e < E_COUNT; ++e) {
        g_level[e] = clamp01(ini_float(kEffects[e].key, static_cast<float>(kEffects[e].def), "rumble") / 100.0f);
        n += snprintf(text + n, sizeof(text) - n, " %s=%.0f", kEffects[e].key, g_level[e] * 100.0f);
    }
    g_ini_time = ini_write_time();
    log_line("%s", text);
}

void save_rumble_levels() {
    for (int e = 0; e < E_COUNT; ++e) {
        char value[16];
        snprintf(value, sizeof(value), "%d", static_cast<int>(lroundf(g_level[e] * 100.0f)));
        WritePrivateProfileStringA("rumble", kEffects[e].key, value, g_ini);
    }
    g_ini_time = ini_write_time();
    log_line("rumble: levels saved");
}

// Called every frame of a race: picks up edits to the ini within half a second.
void reload_rumble_levels_if_changed() {
    static DWORD next_check = 0;
    if (GetTickCount() < next_check) return;
    next_check = GetTickCount() + 500;
    const FILETIME t = ini_write_time();
    if (CompareFileTime(&t, &g_ini_time) != 0) load_rumble_levels();
}

bool enabled() { return g_rumble_enabled && xinput::can_vibrate(); }

void load_settings() {
    g_rumble_enabled = GetPrivateProfileIntA("rumble", "enabled", 1, g_ini) != 0;
    load_rumble_levels();
}

void save_levels() { save_rumble_levels(); }

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
    static const unsigned char *seen[96];
    static int seen_count = 0;
    for (int i = 0; i < seen_count; ++i)
        if (seen[i] == layout) return true;
    if (seen_count < 96) {
        seen[seen_count++] = layout;
        log_line("rumble: surface \"%s\" (%08X): road noise %.1f Hz %.2f degrees at %.0f-%.0f m/s, wheel effect "
                 "frequency %u intensity %u", out->name ? out->name : "?", out->key, out->noise_freq, out->noise_amp,
                 out->noise_min,
                 out->noise_max, out->wheel_freq, out->wheel_intensity);
    }
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
// Options > Rumble's previews are played by the thread itself, so a crash and its preview reach the controller the same
// way. The thread also stops the motors when the game stops updating (loading, a hang).
volatile float g_game_level = 0.0f;
volatile DWORD g_last_rumble_update = 0;  // when the race last set g_game_level
volatile LONG g_preview_effect = -1;      // the effect being previewed, or -1
volatile DWORD g_preview_start = 0;
volatile DWORD g_trace_start = 0;  // log every send for a while after a big jolt (see start_jolt)

// Why the race level was 0, counted for the log (the trace showed it dropping to 0 every other frame).
volatile LONG g_n_updates = 0, g_n_pauses = 0, g_n_resets = 0, g_n_zero_frames = 0;
volatile LONG g_n_cut_vibration = 0, g_n_cut_idle = 0, g_n_cut_focus = 0;  // frames with rumble cut by each check
volatile LONG g_stop_now = 0;  // pause / reset: the motors stop at once, not after the short hold below

void set_motors(float level) { g_game_level = level; }

// The thread's side. A change is sent at most every 30 ms (a rise of a tenth or more, or stopping, at once); a steady
// level is sent again every half second, as Xbox controllers on Bluetooth stop a rumble after a while.
void send_level(float level) {
    static WORD sent = 0;
    static DWORD last_send = 0;
    const int port = xinput::port;
    if (!xinput::can_vibrate() || port < 0) return;
    const WORD v = static_cast<WORD>(clamp01(level) * 65535.0f + 0.5f);
    const DWORD now = GetTickCount();
    const bool changed = abs(v - sent) >= 1024 || (v == 0) != (sent == 0);
    const bool urgent = v == 0 || v >= sent + 6554;
    if (!(changed && (urgent || now - last_send >= 30)) && !(v && now - last_send >= 500)) return;
    xinput::Vibration vib = {v, v};
    const DWORD t0 = GetTickCount();
    const DWORD result = xinput::set_state(port, &vib);
    static int failures = 0;
    if (result != ERROR_SUCCESS && failures++ < 5) log_line("rumble: XInputSetState(%d) failed: %lu", port, result);
    const DWORD trace_start = g_trace_start;
    if (trace_start && static_cast<LONG>(now - trace_start) < 2500)
        log_line("rumble:   sent %3.0f%% at +%4ld ms (took %lu ms)", v * 100.0f / 65535.0f,
                 static_cast<long>(static_cast<LONG>(now - trace_start)), GetTickCount() - t0);
    sent = v;
    last_send = now;
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

DWORD WINAPI rumble_thread(void *) {
    for (;;) {
        // The times come from the other thread: read them before GetTickCount(), and compare signed, so a time set a
        // tick later than our clock reading never looks like one from 49 days ago. (It did once: the motors were cut
        // to 0 every few frames, which made every effect in a race feel weak.)
        float level = 0.0f;
        const LONG effect = g_preview_effect;
        const DWORD preview_start = g_preview_start, last_update = g_last_rumble_update;
        const LONG preview_age = static_cast<LONG>(GetTickCount() - preview_start);
        const LONG update_age = static_cast<LONG>(GetTickCount() - last_update);
        if (effect >= 0 && effect < E_COUNT) {
            float s = 0.0f;
            if (preview_shape(effect, (preview_age > 0 ? preview_age : 0) / 1000.0f, &s)) level = s * g_level[effect];
            else InterlockedCompareExchange(&g_preview_effect, -1, effect);
        } else if (update_age <= 250) {
            level = g_game_level;
        }
        if (InterlockedExchange(&g_stop_now, 0) || (effect < 0 && update_age > 250)) level = 0.0f;
        send_level(level);
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
} g_rs;

void clear_rumble_effects() {
    g_rs.was_nos = g_rs.was_shifting = g_rs.was_blown = false;
    g_rs.was_flat_tires = 0;
    g_rs.was_gear = -1;
    g_rs.jolt = g_rs.jolt_hold = g_rs.scrape = g_rs.scrape_hold = g_rs.redline_time = 0.0f;
    g_rs.shift_time = 1.0f;
    g_rs.last.QuadPart = 0;
}

// A jolt (crash, landing, blowout) of an effect; severity 0..1 is how hard the hit was.
void start_jolt(int effect, float severity) {
    const float v = g_level[effect] * severity;
    if (v <= g_rs.jolt) return;
    g_rs.jolt = v;
    g_rs.jolt_effect = effect;
    static int traced = 0;
    if (v >= 0.5f && traced < 6 && (!g_trace_start || GetTickCount() - g_trace_start > 3000)) {
        ++traced;
        g_trace_start = GetTickCount();
        log_line("rumble: %s jolt at %.0f%%, tracing what is sent:", kEffects[effect].key, v * 100.0f);
    }
    g_rs.jolt_hold = jolt_hold(severity);
    g_rs.jolt_fade = jolt_fade(severity);
}

// Scraping along something (strength 0..1): a grinding rumble at half the effect's level, while it lasts.
void start_scrape(int effect, float strength) {
    const float v = g_level[effect] * 0.5f * strength;
    g_rs.scrape_hold = 0.08f;
    if (v <= g_rs.scrape) return;
    g_rs.scrape = v;
    g_rs.scrape_effect = effect;
}

// The player's Vibration setting, read before each DoFFB (see hook_do_ffb).
bool g_vibration_on = true;

// One log line per event start (NOS, gear change, rev limiter, blowouts), to match the log to what was felt.
void log_rumble_event(const char *what, int value = -1) {
    static int lines = 0;
    if (lines++ >= 300) return;
    if (value >= 0) log_line("rumble: %s %d at %.1f s", what, value, g_rs.time);
    else log_line("rumble: %s at %.1f s", what, g_rs.time);
}

void update_rumble() {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = 1.0f / 60.0f;
    if (g_rs.last.QuadPart)
        dt = static_cast<float>(now.QuadPart - g_rs.last.QuadPart) / static_cast<float>(freq.QuadPart);
    if (dt > 0.1f) dt = 0.1f;
    g_rs.last = now;
    g_rs.time += dt;
    g_last_rumble_update = GetTickCount();
    reload_rumble_levels_if_changed();
    static DWORD rate_start = 0;
    static int rate_count = 0, rate_lines = 0;
    ++rate_count;
    if (GetTickCount() - rate_start >= 5000) {
        if (rate_start && rate_lines++ < 12)
            log_line("rumble: %.0f updates per second from the game; since the last line: %ld pauses, %ld resets, "
                     "%ld zero frames, cut by Vibration %ld / idle controller %ld / focus %ld (of %ld frames)",
                     rate_count * 1000.0f / (GetTickCount() - rate_start), InterlockedExchange(&g_n_pauses, 0),
                     InterlockedExchange(&g_n_resets, 0), InterlockedExchange(&g_n_zero_frames, 0),
                     InterlockedExchange(&g_n_cut_vibration, 0), InterlockedExchange(&g_n_cut_idle, 0),
                     InterlockedExchange(&g_n_cut_focus, 0), InterlockedExchange(&g_n_updates, 0));
        rate_start = GetTickCount();
        rate_count = 0;
    }
    const float *level = g_level;

    // Each effect gives its level times how strong it is right now (0..1, reaching 1 in an ordinary instance of the
    // event: a normal drift, a solid hit). Effects at the same time combine like independent shakes, 1 - (1 - a)(1 - b),
    // so together they never go past full. contrib[] keeps each one's part, for the log.
    float out = 0.0f, contrib[E_COUNT] = {};
    const auto add = [&](int effect, float v) {
        v = clamp01(v);
        out = either(out, v);
        if (v > contrib[effect]) contrib[effect] = v;
    };

    // Rough ground: the roughest wheel, pulsing faster with speed.
    if (g_rs.road > 0.0f) {
        g_rs.road_phase = std::fmod(g_rs.road_phase + g_rs.road_freq * dt, 1.0f);
        add(E_ROAD, level[E_ROAD] * g_rs.road * pulse_at(g_rs.road_phase, 0.6f));
    }
    add(E_DRIFT, level[E_DRIFT] * drift_shape(g_rs.skid));
    add(E_WHEELSPIN, level[E_WHEELSPIN] * wheelspin_shape(g_rs.slip));
    const float pb = g_rs.powerband;
    add(E_ENGINE, level[E_ENGINE] * pb * pb * (0.3f + 0.7f * g_rs.throttle));
    // The rev limiter: held at maximum revs for a while (an automatic gearbox sits there briefly before each upshift).
    const float was_redline_time = g_rs.redline_time;
    g_rs.redline_time = pb > 0.985f && g_rs.throttle > 0.6f ? g_rs.redline_time + dt : 0.0f;
    if (g_rs.redline_time > 0.6f && was_redline_time <= 0.6f) log_rumble_event("rev limiter");
    if (g_rs.redline_time > 0.6f) add(E_REV_LIMITER, level[E_REV_LIMITER] * pulse_at(g_rs.time * 8.0f, 0.4f));
    if (g_rs.blown) {
        if (!g_rs.was_blown) {
            start_jolt(E_ENGINE_BLOWN, 1.0f);
            log_rumble_event("blown engine");
        }
        add(E_ENGINE_BLOWN, level[E_ENGINE_BLOWN] * pulse_at(g_rs.time * 2.5f, 0.2f));
    }
    g_rs.was_blown = g_rs.blown;
    // Tire blowouts: a bang, then the flat tire thumping once per turn of the wheel (about 2 m).
    if (g_rs.flat_tires > g_rs.was_flat_tires) {
        start_jolt(E_TIRE_BLOWOUT, 1.0f);
        log_rumble_event("tire blowout, flat tires:", g_rs.flat_tires);
    }
    g_rs.was_flat_tires = g_rs.flat_tires;
    if (g_rs.flat_tires > 0 && g_rs.speed > 1.0f) {
        const float rate = g_rs.speed / 2.0f < 8.0f ? g_rs.speed / 2.0f : 8.0f;
        g_rs.tire_phase = std::fmod(g_rs.tire_phase + rate * dt, 1.0f);
        add(E_TIRE_BLOWOUT, level[E_TIRE_BLOWOUT] * ramp(g_rs.speed, 1.0f, 10.0f) *
                                (0.25f + 0.75f * pulse_at(g_rs.tire_phase, 0.0f)));
    }
    if (g_rs.nos) {
        if (!g_rs.was_nos) {
            g_rs.nos_time = 0.0f;
            log_rumble_event("NOS");
        }
        g_rs.nos_time += dt;
        add(E_NOS, level[E_NOS] * nos_shape(g_rs.nos_time));
    }
    g_rs.was_nos = g_rs.nos;
    // Upshifts, from the transmission's gear (forward gears only); the game's own report (which also includes
    // downshifts) only when the gear is unknown.
    const bool upshift = g_rs.gear >= 2 && g_rs.was_gear >= 2 && g_rs.gear > g_rs.was_gear;
    if (upshift) log_rumble_event("shift up to gear", g_rs.gear - 1);
    if (upshift || (g_rs.gear < 0 && g_rs.shifting && !g_rs.was_shifting)) g_rs.shift_time = 0.0f;
    if (g_rs.gear >= 0) g_rs.was_gear = g_rs.gear;
    g_rs.was_shifting = g_rs.shifting;
    if (g_rs.shift_time < 0.5f) {
        add(E_SHIFTS, level[E_SHIFTS] * shift_shape(g_rs.shift_time));
        g_rs.shift_time += dt;
    }
    // Jolts (collisions, landings, blowouts) and scraping: started by events, carrying their own level.
    if (g_rs.jolt > 0.0f) {
        add(g_rs.jolt_effect, g_rs.jolt);
        if (g_rs.jolt_hold > 0.0f) g_rs.jolt_hold -= dt;
        else g_rs.jolt *= std::exp(-dt / g_rs.jolt_fade);
        if (g_rs.jolt < 0.02f) g_rs.jolt = 0.0f;
    }
    if (g_rs.scrape > 0.0f) {
        add(g_rs.scrape_effect, g_rs.scrape * pulse_at(g_rs.time * 10.0f, 0.5f));
        if (g_rs.scrape_hold > 0.0f) g_rs.scrape_hold -= dt;
        else g_rs.scrape *= std::exp(-dt / 0.05f);
        if (g_rs.scrape < 0.02f) g_rs.scrape = 0.0f;
    }

    InterlockedIncrement(&g_n_updates);
    if (out > 0.0f) {
        volatile LONG *cut = !g_vibration_on ? &g_n_cut_vibration
                             : !pad_in_use() ? &g_n_cut_idle
                             : !game_in_foreground() ? &g_n_cut_focus
                                                     : nullptr;
        if (cut) {
            InterlockedIncrement(cut);
            out = 0.0f;
        }
    }
    if (out == 0.0f) InterlockedIncrement(&g_n_zero_frames);
    set_motors(out);

    // Every 2 seconds while rumbling: what the game reported and which effects made up the rumble.
    static DWORD next_log = 0;
    static int log_lines = 0;
    if (out > 0.02f && GetTickCount() >= next_log && log_lines < 400) {
        next_log = GetTickCount() + 2000;
        ++log_lines;
        char parts[256];
        int n = 0;
        parts[0] = 0;
        for (int e = 0; e < E_COUNT; ++e)
            if (contrib[e] >= 0.02f)
                n += snprintf(parts + n, sizeof(parts) - n, " %s %.0f", kEffects[e].key, contrib[e] * 100.0f);
        log_line("rumble: %.0f m/s, road %.2f, skid %.1f, slip %.1f, revs %.2f, throttle %.2f%s -> %.0f%% (%s )",
                 g_rs.speed, g_rs.road, g_rs.skid, g_rs.slip, pb, g_rs.throttle, g_rs.nos ? ", NOS" : "",
                 out * 100.0f, parts);
    }
}

struct Feedback {
    void **vtbl;
    void *inner;  // the game's object, which every call is passed on to
} g_feedback;

template <typename... A>
void pass_on(int index, A... args) {
    if (!g_feedback.inner) return;
    using Fn = void(FASTCALL *)(void *, void *, A...);
    reinterpret_cast<Fn>((*static_cast<void ***>(g_feedback.inner))[index])(g_feedback.inner, nullptr, args...);
}

// Slot 0, the deleting destructor: never called, the game does not own this object.
void *FASTCALL fb_destroy(Feedback *self, void *, int) { return self; }
void FASTCALL fb_pause(Feedback *, void *) {
    pass_on(1);
    InterlockedIncrement(&g_n_pauses);
    static bool logged = false;
    if (!logged) log_line("rumble: first pause from the game (paused, AI control, or the profile's Rumble setting off)");
    logged = true;
    clear_rumble_effects();
    set_motors(0.0f);
    g_stop_now = 1;
}
void FASTCALL fb_resume(Feedback *, void *) { pass_on(2); }
void FASTCALL fb_reset(Feedback *, void *) {
    pass_on(3);
    InterlockedIncrement(&g_n_resets);
    clear_rumble_effects();
    set_motors(0.0f);
    g_stop_now = 1;
}
void FASTCALL fb_begin(Feedback *, void *) {
    pass_on(4);
    g_rs.road = g_rs.road_freq = g_rs.speed = g_rs.skid = g_rs.slip = g_rs.powerband = g_rs.throttle = 0.0f;
    g_rs.nos = g_rs.shifting = g_rs.blown = false;
    g_rs.flat_tires = 0;
}
void FASTCALL fb_end(Feedback *, void *) {
    pass_on(5);
    static bool logged = false;
    if (!logged) log_line("rumble: first update from the game");
    logged = true;
    update_rumble();
}
void FASTCALL fb_road_noise(Feedback *, void *, int front, const void *surface, float speed) {
    pass_on(6, front, surface, speed);
    g_rs.speed = speed;
    SurfaceData d;
    if (!read_surface(surface, &d)) return;
    if (d.key == kBlownTireSurface) ++g_rs.flat_tires;
    if (!on_ground(d)) return;
    // How rough: the renderer's body shake in degrees (wood, grass and gravel: 1), or for surfaces without one (the
    // railroad) the console rumble intensity, about 100 for rough ground. Full from 15 m/s, pulsing faster with speed.
    const float shake = d.noise_freq * d.noise_amp * d.noise_max > 0.0f ? d.noise_amp : 0.0f;
    const float k = ramp(speed, 0.0f, 15.0f);
    const float rough = clamp01((shake > d.wheel_intensity / 110.0f ? shake : d.wheel_intensity / 110.0f) * k);
    if (rough > g_rs.road) {
        g_rs.road = rough;
        g_rs.road_freq = (d.noise_freq > 0.0f ? d.noise_freq : 8.0f) * (k > 0.3f ? k : 0.3f);
    }
}
void FASTCALL fb_tire_skid(Feedback *, void *, int front, const void *surface, float skid) {
    pass_on(7, front, surface, skid);
    if (skid > g_rs.skid && on_ground(surface)) g_rs.skid = skid;
}
void FASTCALL fb_tire_slip(Feedback *, void *, int front, const void *surface, float slip) {
    pass_on(8, front, surface, slip);
    if (slip > g_rs.slip && on_ground(surface)) g_rs.slip = slip;
}
void FASTCALL fb_rpm(Feedback *, void *, float powerband, float overrev, float throttle) {
    pass_on(9, powerband, overrev, throttle);
    g_rs.powerband = clamp01(powerband);
    g_rs.throttle = clamp01(throttle);
}
void FASTCALL fb_shift_potential(Feedback *, void *, int potential) { pass_on(10, potential); }
void FASTCALL fb_nos(Feedback *, void *, int engaged, float capacity) {
    pass_on(11, engaged, capacity);
    g_rs.nos = (engaged & 0xFF) != 0;
}
void FASTCALL fb_engine_blown(Feedback *, void *, int blown) {
    pass_on(12, blown);
    g_rs.blown = (blown & 0xFF) != 0;
}
void FASTCALL fb_shifting(Feedback *, void *, int shifting) {
    pass_on(13, shifting);
    g_rs.shifting = (shifting & 0xFF) != 0;
}

// COLLISION_INFO (Sim::Collision::Info): bits at +0x1C (type in bits 0-2: 1 object, 2 world, 3 ground; sliding in
// bit 7), closing velocity +0x20, force +0x2C, impulseA +0x5C and impulseB +0x6C (each body's change of speed, m/s),
// sliding velocity +0x70.
void FASTCALL fb_collision(Feedback *, void *, const unsigned char *info, int iam_a) {
    pass_on(14, info, iam_a);
    if (!info) return;
    const unsigned bits = *reinterpret_cast<const unsigned *>(info + 0x1C);
    const float *closing = reinterpret_cast<const float *>(info + 0x20);
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
    static int logged = 0;
    if ((dv > 0.3f || sliding) && logged < 80) {
        ++logged;
        log_line("rumble: collision type %u%s%s, speed change %.2f m/s, closing %.1f m/s, force %.0f, sliding %.1f m/s "
                 "-> severity %.2f", bits & 7, (iam_a & 0xFF) ? "" : " (as B)", sliding ? ", sliding" : "", dv,
                 std::sqrt(closing[0] * closing[0] + closing[1] * closing[1] + closing[2] * closing[2]),
                 *reinterpret_cast<const float *>(info + 0x2C), sliding_speed, severity);
    }
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

// The player's Vibration setting: PlayerSettings (LocalPlayer::GetSettings 0x6E99A0, this = the view at +0x4C of
// DoFFB's) +0x4. The PC build's CanDoFFB fetches the settings but never checks the flag (only OnCollision does), so
// DoFFB keeps feeding the feedback object with Vibration off.
constexpr uintptr_t kGetSettings = 0x6E99A0;
bool vibration_setting(void *player) {
    const auto get = reinterpret_cast<const unsigned char *(FASTCALL *)(void *, void *)>(kGetSettings);
    const unsigned char *settings = get(static_cast<char *>(player) + 0x4C, nullptr);
    return !settings || settings[0x4] != 0;
}

using DoFfbFn = void(FASTCALL *)(void *, void *);
DoFfbFn g_orig_do_ffb;
void FASTCALL hook_do_ffb(void *player, void *edx) {
    if (field<void *>(player, 0x58) == &g_feedback) {
        g_rs.gear = player_gear(player);
        g_vibration_on = vibration_setting(player);
    }
    void *&wheel = field<void *>(player, 0x5C);
    void *const saved = wheel;
    const bool hide = saved && field<void *>(player, 0x58) == &g_feedback && input::only_xinput_controllers();
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
    g_orig_set_controller_port(player, edx, port);
    void *&ffb = field<void *>(player, 0x0C);
    if (!g_rumble_enabled || !xinput::can_vibrate() || port < 0 || ffb == &g_feedback) return;
    g_feedback.vtbl = g_feedback_vtbl;
    g_feedback.inner = ffb;
    ffb = &g_feedback;
    clear_rumble_effects();
    log_line("rumble: the player on port %d now rumbles through nfsmw_controller_support (the game's feedback object: %p)", port,
             g_feedback.inner);
}


void play_preview(int effect) {
    static int traced = 0;
    if (traced < 4 && (!g_trace_start || GetTickCount() - g_trace_start > 3000)) {
        ++traced;
        g_trace_start = GetTickCount();
        log_line("rumble: preview of %s at %.0f%%, tracing what is sent:", kEffects[effect].key, g_level[effect] * 100.0f);
    }
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

}  // namespace pad::rumble

// XInput, loaded at run time (whichever version Windows has): reading the pad, and the rumble motors. Header only;
// each mod that includes it has its own copy of the state.
#pragma once

#include "nfsmw_util.h"

namespace nfsmw::xinput {

struct Gamepad {
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT lx, ly, rx, ry;
};
struct State {
    DWORD packet;
    Gamepad pad;
};
struct Vibration {
    WORD left, right;  // the heavy (low frequency) and the light (high frequency) motor
};
// Gamepad::buttons
constexpr WORD kDpadUp = 0x0001, kDpadDown = 0x0002, kDpadLeft = 0x0004, kDpadRight = 0x0008, kStart = 0x0010,
               kBack = 0x0020, kLeftThumb = 0x0040, kRightThumb = 0x0080, kLeftShoulder = 0x0100,
               kRightShoulder = 0x0200, kButtonA = 0x1000, kButtonB = 0x2000, kButtonX = 0x4000, kButtonY = 0x8000;

using GetStateFn = DWORD(WINAPI *)(DWORD, State *);
using SetStateFn = DWORD(WINAPI *)(DWORD, Vibration *);
inline GetStateFn get_state = nullptr;
inline SetStateFn set_state = nullptr;
inline int port = -1;        // the controller in use, or -1
inline DWORD next_scan = 0;  // polling an empty port is slow: look for a controller at most every 2 seconds
inline Logger *log = nullptr;

inline void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
inline void logf(const char *fmt, ...) {
    if (!log) return;
    va_list ap;
    va_start(ap, fmt);
    log->vline(fmt, ap);
    va_end(ap);
}

inline bool load(Logger *logger) {
    log = logger;
    for (const char *dll : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"}) {
        if (HMODULE h = LoadLibraryA(dll)) {
            get_state = reinterpret_cast<GetStateFn>(reinterpret_cast<void *>(GetProcAddress(h, "XInputGetState")));
            set_state = reinterpret_cast<SetStateFn>(reinterpret_cast<void *>(GetProcAddress(h, "XInputSetState")));
            if (get_state) {
                logf("XInput: %s", dll);
                return true;
            }
        }
    }
    logf("XInput: not available");
    return false;
}

inline bool can_vibrate() { return get_state && set_state; }

// The first connected controller's state. False when there is none (an empty port is only looked at every 2 seconds).
inline bool read(Gamepad *out) {
    if (!get_state) return false;
    State s;
    if (port >= 0) {
        if (get_state(port, &s) == ERROR_SUCCESS) {
            *out = s.pad;
            return true;
        }
        logf("XInput controller %d disconnected", port);
        port = -1;
        next_scan = GetTickCount() + 2000;
        return false;
    }
    if (static_cast<LONG>(GetTickCount() - next_scan) < 0) return false;
    next_scan = GetTickCount() + 2000;
    for (int i = 0; i < 4; ++i) {
        if (get_state(i, &s) == ERROR_SUCCESS) {
            port = i;
            logf("XInput controller %d connected", i);
            *out = s.pad;
            return true;
        }
    }
    return false;
}

// One slot's state, whichever controller `port` is. False when nothing is connected there; the caller paces how often
// it asks about an empty slot (that is slow).
inline bool read_slot(int slot, Gamepad *out) {
    State s;
    if (!get_state || slot < 0 || slot > 3 || get_state(slot, &s) != ERROR_SUCCESS) return false;
    *out = s.pad;
    return true;
}

// All connected controllers as one: any button, and each trigger and stick axis from the controller pushing it furthest. Empty slots are
// asked about at most every 2 seconds (that is slow). False when none is connected. (`read` is the first controller's only.) `each`, if given,
// is called with every connected controller's slot and state.
inline bool read_any(Gamepad *out, void (*each)(int slot, const Gamepad &pad) = nullptr) {
    static bool connected[4];
    static DWORD next = 0;
    const bool scan = static_cast<LONG>(GetTickCount() - next) >= 0;
    if (scan) next = GetTickCount() + 2000;
    const auto furthest = [](SHORT a, SHORT b) { return (b < 0 ? -b : b) > (a < 0 ? -a : a) ? b : a; };
    Gamepad merged = {};
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        if (!connected[i] && !scan) continue;
        Gamepad p;
        connected[i] = read_slot(i, &p);
        if (!connected[i]) continue;
        any = true;
        if (each) each(i, p);
        merged.buttons |= p.buttons;
        if (p.left_trigger > merged.left_trigger) merged.left_trigger = p.left_trigger;
        if (p.right_trigger > merged.right_trigger) merged.right_trigger = p.right_trigger;
        merged.lx = furthest(merged.lx, p.lx);
        merged.ly = furthest(merged.ly, p.ly);
        merged.rx = furthest(merged.rx, p.rx);
        merged.ry = furthest(merged.ry, p.ry);
    }
    *out = merged;
    return any;
}

}  // namespace nfsmw::xinput

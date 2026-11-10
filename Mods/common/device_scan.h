// How often to look for controllers that were plugged in. The looks are slow: DirectInput's EnumDevices opens every HID device of the system (a few
// milliseconds on most machines, tens to hundreds with some devices and drivers), and XInputGetState on an empty slot is slow too (Microsoft:
// space such checks out). When mets_controller_support hears of Windows' device changes (its device_watch, export below), the looks are made only
// for a few seconds after a change, so nothing is looked for while nothing is plugged in or out; otherwise at most every 2 seconds. Header only;
// each module (each .asi) has its own copy, and finds mets_controller_support by its export.
#pragma once

#include <windows.h>

namespace nfsmw::device_scan {

// mets_controller_support's export: how many times Windows reported a device arriving or going, or -1 when it does not hear of them.
constexpr const char *kChangesExport = "mets_controller_support_device_changes";
using ChangesFn = int (*)();

inline ChangesFn changes_source() {
    static ChangesFn source = nullptr;
    static bool looked_up = false;
    if (!looked_up) {
        looked_up = true;
        if (HMODULE module = GetModuleHandleA("mets_controller_support.asi"))
            source = reinterpret_cast<ChangesFn>(reinterpret_cast<void *>(GetProcAddress(module, kChangesExport)));
    }
    return source;
}

constexpr DWORD kInterval = 2000;     // without device changes to go by
constexpr DWORD kAfterChange = 5000;  // with them: for this long after one (a new device's driver takes a moment) ...
constexpr DWORD kAfterChangeInterval = 2000;  // ... this often (at 0, 2 and 4 seconds)

// One caller's pace: whether it is time for its look now.
struct Pace {
    DWORD next = 0, until = 0;
    int seen = -2;  // the count of changes the last window was opened for (-2: none yet, so the first call looks)
};

inline bool due(Pace &pace) {
    const DWORD now = GetTickCount();
    const ChangesFn source = changes_source();
    const int changes = source ? source() : -1;
    if (changes >= 0) {
        if (changes != pace.seen) {
            pace.seen = changes;
            pace.until = now + kAfterChange;
            pace.next = now;
        }
        if (static_cast<LONG>(now - pace.until) >= 0 || static_cast<LONG>(now - pace.next) < 0) return false;
        pace.next = now + kAfterChangeInterval;
        return true;
    }
    if (static_cast<LONG>(now - pace.next) < 0) return false;
    pace.next = now + kInterval;
    return true;
}

}  // namespace nfsmw::device_scan

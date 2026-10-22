// Keyboard helpers for the mods' hot keys (polled with GetAsyncKeyState, only while the game's window has the focus). Header only.
#pragma once

#include <windows.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace nfsmw {

// An ini value for a key: F1 to F12, a letter or digit, one of the names below, or a virtual-key code number.
inline int key_from_name(const char *name) {
    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '1' && name[1] <= '9') {
        const int n = atoi(name + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    if (name[0] && !name[1]) return toupper(static_cast<unsigned char>(name[0]));
    static const struct {
        const char *name;
        int vk;
    } kNames[] = {{"Minus", VK_OEM_MINUS}, {"Plus", VK_OEM_PLUS}, {"Equals", VK_OEM_PLUS}, {"Comma", VK_OEM_COMMA}, {"Period", VK_OEM_PERIOD}, {"PageUp", VK_PRIOR},
                  {"PageDown", VK_NEXT}, {"Home", VK_HOME}, {"End", VK_END}, {"Insert", VK_INSERT}, {"Delete", VK_DELETE}, {"NumpadPlus", VK_ADD}, {"NumpadMinus", VK_SUBTRACT}};
    for (const auto &entry : kNames)
        if (!_stricmp(name, entry.name)) return entry.vk;
    return atoi(name);
}

// The foreground window belongs to this process (and is returned in `window` if given).
inline bool game_has_focus(HWND *window = nullptr) {
    const HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    if (window) *window = foreground;
    return foreground && pid == GetCurrentProcessId();
}

inline bool key_down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; }

// True once for a click (mouse button or key) of the game's window: when it goes down, or when it has been pressed and let go since the last call (a click shorter than a
// frame, which polling the state once a frame misses). `before` is the caller's memory of the last call.
inline bool key_clicked(int vk, bool &before) {
    const SHORT state = vk ? GetAsyncKeyState(vk) : 0;
    HWND window;
    const bool focus = state && game_has_focus(&window);
    const bool down = focus && (state & 0x8000) != 0;
    const bool edge = (down && !before) || (focus && (state & 1) != 0);
    before = down;
    return edge;
}

// True once when `now` turns on; `before` is the caller's memory of the last call.
inline bool key_pressed(bool now, bool &before) {
    const bool edge = now && !before;
    before = now;
    return edge;
}

}  // namespace nfsmw

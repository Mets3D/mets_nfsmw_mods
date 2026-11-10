// A user's game froze briefly every 2 seconds, in the menus and in races: the looks for new controllers (DirectInput's EnumDevices in
// new_controllers.cpp, XInputGetState on the empty slots) ran every 2 seconds on the game's thread, and EnumDevices opens every HID device of the
// system, which takes long with some devices. Windows tells a window when a device interface arrives or goes (WM_DEVICECHANGE, for windows that
// registered with RegisterDeviceNotification; a message-only window may, it gets no broadcasts). This thread owns such a window and counts the
// changes; common/device_scan.h makes the looks only for a few seconds after one. The count is exported for the other mods (split screen, custom menus).
#include "device_watch.h"

#include <windows.h>
#include <dbt.h>

#include "pad.h"

namespace pad::device_watch {

namespace {

volatile LONG g_changes = 0;
volatile LONG g_listening = 0;

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM what, LPARAM detail) {
    if (message == WM_DEVICECHANGE && (what == DBT_DEVICEARRIVAL || what == DBT_DEVICEREMOVECOMPLETE || what == DBT_DEVNODES_CHANGED))
        InterlockedIncrement(&g_changes);
    return DefWindowProcA(window, message, what, detail);
}

DWORD WINAPI listen(void *) {
    HMODULE module = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&window_proc), &module);
    WNDCLASSA window_class = {};
    window_class.lpfnWndProc = &window_proc;
    window_class.hInstance = module;
    window_class.lpszClassName = "mets_controller_support_device_watch";
    RegisterClassA(&window_class);
    HWND window = CreateWindowExA(0, window_class.lpszClassName, "", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, module, nullptr);
    DEV_BROADCAST_DEVICEINTERFACE_A filter = {};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    HDEVNOTIFY notify = window ? RegisterDeviceNotificationA(window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES) : nullptr;
    if (!notify) {
        log_line("device changes: Windows does not tell (%s, error %lu): controllers are looked for every 2 seconds", window ? "no notifications" : "no window",
                 GetLastError());
        if (window) DestroyWindow(window);
        return 0;
    }
    InterlockedExchange(&g_listening, 1);
    log_line("device changes: Windows tells when a device is plugged in or out; controllers are looked for only then");
    MSG message;
    while (GetMessageA(&message, nullptr, 0, 0) > 0) DispatchMessageA(&message);
    return 0;
}

}  // namespace

void start() {
    HANDLE thread = CreateThread(nullptr, 0, &listen, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
    else log_line("device changes: no thread to listen (error %lu): controllers are looked for every 2 seconds", GetLastError());
}

}  // namespace pad::device_watch

// For common/device_scan.h (this mod's and the others'): the count of device changes, -1 while Windows does not tell of them.
extern "C" __declspec(dllexport) int mets_controller_support_device_changes() {
    return pad::device_watch::g_listening ? static_cast<int>(pad::device_watch::g_changes) : -1;
}

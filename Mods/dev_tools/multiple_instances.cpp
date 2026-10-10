/*
 * The game's main() starts with IsTheGameAlreadyRunning("speed.exe", 1) (0x6CBC00): it lists the running processes, counts
 * the ones with that exe name and, when there are more than the allowed one, waits up to three seconds for the other to go
 * (one second, then a new count, three times) and then ends the game.
 *
 * Plugins are loaded after main() has begun (the ASI loader starts them later), so the first count is already done when
 * this plugin is in. What it can still do, within those three seconds, is make the next count fail: the check takes "I
 * cannot list the processes" for "not running". The function itself is replaced too, for loaders that start plugins early.
 */
#include "multiple_instances.h"

#include <MinHook.h>
#include <tlhelp32.h>

#include "dev_tools.h"
#include "hooks.h"

namespace dev_tools::multiple_instances {

namespace {

// bool __cdecl IsTheGameAlreadyRunning(const char *exe_name, int allowed_count)
using AlreadyRunningFn = bool(__cdecl *)(const char *, int);
AlreadyRunningFn g_orig_already_running;  // not called: it would wait for the other copy to go

bool __cdecl hook_already_running(const char *exe_name, int allowed_count) {
    g_log.line("IsTheGameAlreadyRunning(\"%s\", %d): answered no", exe_name ? exe_name : "?", allowed_count);
    return false;
}

// The process list the check asks for (TH32CS_SNAPPROCESS): refused, so that the check's next count finds nothing wrong.
using SnapshotFn = HANDLE(WINAPI *)(DWORD, DWORD);
SnapshotFn g_orig_snapshot;
HANDLE WINAPI hook_snapshot(DWORD flags, DWORD process) {
    if (flags & TH32CS_SNAPPROCESS) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            g_log.line("CreateToolhelp32Snapshot(processes): refused (%lu ms after the process started)", since_start_ms());
        }
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    return g_orig_snapshot(flags, process);
}

}  // namespace

void install() {
    const nfsmw::Hook hooks[] = {
        {"alreadyrunning", "IsTheGameAlreadyRunning", 0x6CBC00, {0x81, 0xEC, 0x2C, 0x02, 0x00, 0x00, 0x53}, true,
         nfsmw::detour(&hook_already_running), nfsmw::orig(&g_orig_already_running)},
    };
    const nfsmw::HookResult result = nfsmw::install_hooks(hooks, 1, g_log, g_ini, "multiple instances");
    if (!result.ok) g_log.line("the check was not removed: a second copy will not start");
    if (MH_CreateHookApi(L"kernel32", "CreateToolhelp32Snapshot", reinterpret_cast<void *>(&hook_snapshot),
                         reinterpret_cast<void **>(&g_orig_snapshot)) == MH_OK)
        MH_EnableHook(MH_ALL_HOOKS);
    else
        g_log.line("could not hook CreateToolhelp32Snapshot");
}

}  // namespace dev_tools::multiple_instances

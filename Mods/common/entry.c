/*
 * entry.c: the loader entry of every plugin (the SDK's src/entry.c, which this replaces, with an early entry added).
 *
 * Ultimate ASI Loader loads the .asi in the game process before the game's own code has run. The plugin then runs
 * in two steps:
 *   - nfsmw_plugin_early_ptr (optional, see early_entry.h): at once, in DllMain, on the game's main thread. For what
 *     must be in place before the game gets going (the intro movies are decided within about a second). DllMain holds the
 *     loader lock: no LoadLibrary, no waiting on other threads.
 *   - nfsmw_plugin_main_ptr (NFSMW_PLUGIN_MAIN): from a worker thread after a delay, to let the engine finish its
 *     start-up. The mains of all plugins would start at the same moment, and installing a hook suspends every other thread of the
 *     process (MinHook): two plugins doing that at once suspend each other (or one a thread that holds the heap's lock), and the
 *     game freezes at start-up. So the mains take turns, through a mutex all the plugins share by name.
 */

#include <nfsmw_sdk/platform.h>

extern int (*nfsmw_plugin_main_ptr)(void);
__attribute__((weak)) int (*nfsmw_plugin_early_ptr)(void) = 0;

#ifndef NFSMW_ASI_INIT_DELAY_MS
#define NFSMW_ASI_INIT_DELAY_MS 2000
#endif

static DWORD WINAPI nfsmw_asi_worker(LPVOID lpv) {
    (void)lpv;
    Sleep(NFSMW_ASI_INIT_DELAY_MS);
    if (nfsmw_validate_speed_exe() && nfsmw_plugin_main_ptr) {
        HANDLE turn = CreateMutexA(NULL, FALSE, "nfsmw_mods_plugin_main");
        const DWORD got = turn ? WaitForSingleObject(turn, 30000) : WAIT_FAILED;  // a plugin that never returns must not hold the others back for ever
        nfsmw_plugin_main_ptr();
        if (turn) {
            if (got == WAIT_OBJECT_0 || got == WAIT_ABANDONED) ReleaseMutex(turn);
            CloseHandle(turn);
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        if (nfsmw_plugin_early_ptr && nfsmw_validate_speed_exe()) nfsmw_plugin_early_ptr();
        HANDLE h = CreateThread(NULL, 0, nfsmw_asi_worker, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
    return TRUE;
}

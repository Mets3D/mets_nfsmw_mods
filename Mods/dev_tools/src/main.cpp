/*
 * nfsmw_dev_tools: things that are useful while making mods for speed.exe 1.3 and not much outside of that. Each part has
 * its own file, header and namespace, and its own switch in nfsmw_dev_tools.ini:
 *   diagnostics.cpp         where the game ends itself and where it has an access violation
 *   quick_start.cpp         skips the boot flow (EA logo, intro movies, splash screen) straight to the main menu
 *   free_camera.cpp         a camera that flies around the track during a race (F1), moves the car to where it is (the speedbreaker key)
 *   game_speed.cpp          two keys that slow the game down and speed it up
 *
 * Files in <game>/scripts/mets_mods/: nfsmw_dev_tools.log, nfsmw_dev_tools.ini (optional).
 */
#include "dev_tools.h"

#include "diagnostics.h"
#include "free_camera.h"
#include "game_speed.h"
#include "quick_start.h"
#include "early_entry.h"

NFSMW_PLUGIN_DECLARE("NFSMW Dev Tools", "1.0.0", "Demeter Dzadik")

namespace dev_tools {

nfsmw::Logger g_log;
char g_ini[MAX_PATH];

unsigned long since_start_ms() {
    FILETIME created, exited, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    GetSystemTimeAsFileTime(&now);
    const unsigned long long a = (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    const unsigned long long b = (static_cast<unsigned long long>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    return static_cast<unsigned long>((b - a) / 10000);
}

// The first of the two entries (early, then main) to run opens the log and finds the ini.
void init() {
    static bool done = false;
    if (done) return;
    done = true;
    g_log.open_in_data_folder("nfsmw_dev_tools.log");
    g_log.line("nfsmw_dev_tools 1.0.0 starting (process %lu, %lu ms after it started)", GetCurrentProcessId(), since_start_ms());
    nfsmw::data_file("nfsmw_dev_tools.ini", g_ini, sizeof(g_ini));
}

}  // namespace dev_tools

// Skipping the movies must be in place before the first one is decided, which is about a second after the game starts.
NFSMW_PLUGIN_EARLY() {
    using namespace dev_tools;
    init();
    if (ini_flag("quick_start", "enabled")) quick_start::install_early();
    return NFSMW_OK;
}

NFSMW_PLUGIN_MAIN() {
    using namespace dev_tools;
    init();
    if (ini_flag("diagnostics", "enabled")) diagnostics::install();
    if (ini_flag("quick_start", "enabled")) quick_start::install();
    if (ini_flag("free_camera", "enabled")) free_camera::install();
    if (ini_flag("game_speed", "enabled")) game_speed::install();
    return NFSMW_OK;
}

/*
 * nfsmw_dev_tools: things that are useful while making mods for speed.exe 1.3 and not much outside of that. Each part has
 * its own file, header and namespace, and its own switch in nfsmw_dev_tools.ini:
 *   multiple_instances.cpp  lets the game start more than once on one PC
 *   network.cpp             the game's network calls: a log, and what two copies on one PC need to find each other
 *   diagnostics.cpp         where the game ends itself and where it has an access violation
 *   quick_start.cpp         skips the boot flow (EA logo, intro movies) straight to the main menu
 *
 * Files in <game>/scripts/mets_mods/: nfsmw_dev_tools.log, nfsmw_dev_tools.ini (optional).
 */
#include "dev_tools.h"

#include "diagnostics.h"
#include "multiple_instances.h"
#include "network.h"
#include "quick_start.h"

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

}  // namespace dev_tools

NFSMW_PLUGIN_MAIN() {
    using namespace dev_tools;
    g_log.open_in_data_folder("nfsmw_dev_tools.log");
    g_log.line("nfsmw_dev_tools 1.0.0 starting (process %lu, %lu ms after it started)", GetCurrentProcessId(), since_start_ms());
    nfsmw::data_file("nfsmw_dev_tools.ini", g_ini, sizeof(g_ini));
    if (ini_flag("multiple_instances", "enabled")) multiple_instances::install();
    if (ini_flag("diagnostics", "enabled")) diagnostics::install();
    if (ini_flag("network", "log")) network::install();
    if (ini_flag("quick_start", "enabled")) quick_start::install();
    return NFSMW_OK;
}

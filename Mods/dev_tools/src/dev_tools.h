// Shared by the parts of nfsmw_dev_tools: the log, the ini and a few helpers. Each part has its own header and namespace.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace dev_tools {

extern nfsmw::Logger g_log;
extern char g_ini[MAX_PATH];  // nfsmw_dev_tools.ini
// Set when the game is ending the process: nothing may suspend its threads from then on (a thread that is suspended when ExitProcess
// kills the others stays suspended, and the process never ends).
extern volatile bool g_exiting;

inline bool ini_flag(const char *section, const char *key, bool def = true) {
    return GetPrivateProfileIntA(section, key, def ? 1 : 0, g_ini) != 0;
}

// Milliseconds since this process was created.
unsigned long since_start_ms();

// Opens the log and finds the ini, once.
void init();

}  // namespace dev_tools

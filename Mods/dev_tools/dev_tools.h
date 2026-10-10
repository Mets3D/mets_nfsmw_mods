// Shared by the parts of nfsmw_dev_tools: the log, the ini and a few helpers. Each part has its own header and namespace.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace dev_tools {

extern nfsmw::Logger g_log;
extern char g_ini[MAX_PATH];  // nfsmw_dev_tools.ini

inline bool ini_flag(const char *section, const char *key, bool def = true) {
    return GetPrivateProfileIntA(section, key, def ? 1 : 0, g_ini) != 0;
}

// Milliseconds since this process was created.
unsigned long since_start_ms();

}  // namespace dev_tools

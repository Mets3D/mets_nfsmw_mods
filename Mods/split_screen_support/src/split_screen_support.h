// Shared by the parts of nfsmw_split_screen_support: the log and the ini. Each part has its own header and namespace.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace split_screen_support {

extern nfsmw::Logger g_log;
extern char g_ini[MAX_PATH];  // nfsmw_split_screen_support.ini

inline bool ini_flag(const char *section, const char *key, bool def = true) {
    return GetPrivateProfileIntA(section, key, def ? 1 : 0, g_ini) != 0;
}

}  // namespace split_screen_support

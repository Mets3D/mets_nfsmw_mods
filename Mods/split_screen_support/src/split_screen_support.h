// Shared by the parts of nfsmw_split_screen_support: the log and the ini. Each part has its own header and namespace.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace split_screen_support {

extern nfsmw::Logger g_log;
extern char g_ini[MAX_PATH];  // nfsmw_split_screen_support.ini

// A copy of a game object (a view's camera or PlatInfo, which the game keeps in static memory) in memory of our own, or null.
inline void *copy_of(void *original, size_t size) {
    void *copy = original ? VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
    if (copy) memcpy(copy, original, size);
    return copy;
}

inline bool ini_flag(const char *section, const char *key, bool def = true) {
    return GetPrivateProfileIntA(section, key, def ? 1 : 0, g_ini) != 0;
}

}  // namespace split_screen_support

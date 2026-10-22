// Shared by the parts of nfsmw_improved_ai_opponents: the log and the ini. Each part has its own header and namespace.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include <mutex>

#include "nfsmw_util.h"
#include "road_network.h"

namespace improved_ai_opponents {

namespace road = nfsmw::road;

extern nfsmw::Logger g_log;
extern char g_ini[MAX_PATH];  // nfsmw_improved_ai_opponents.ini
extern bool g_debug_log;      // [debug] log: lines for finding out what the AI does (each route search, each new route segment, the biggest route costs)
extern bool g_active;         // the parts are installed (nfsmw_split_screen_support was there): the Options page is only offered then

// A lock for data that the game thread and our background threads share (std::lock_guard<Mutex>).
class Mutex {
  public:
    Mutex() { InitializeCriticalSection(&section_); }
    void lock() { EnterCriticalSection(&section_); }
    void unlock() { LeaveCriticalSection(&section_); }

  private:
    CRITICAL_SECTION section_;
};

constexpr int kPathRacer = 2, kPathPlayer = 4;  // WRoadNav path types: an opponent's racer, a player's car (the AI of a human)

inline bool ini_flag(const char *section, const char *key, bool def = true) {
    return GetPrivateProfileIntA(section, key, def ? 1 : 0, g_ini) != 0;
}

}  // namespace improved_ai_opponents

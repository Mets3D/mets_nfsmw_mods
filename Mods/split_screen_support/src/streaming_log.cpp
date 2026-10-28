/*
 * What a bug report about missing geometry needs: the state of the track streamer, the game's memory, and where every player's car is.
 *   - F11 (`[options] streaming_dump_key`, a virtual key code; 0 = none; F7 and F8 are taken by other mods, F9 by this one) writes a dump to the log when pressed: the streamer (the pool, the sections loaded, in memory
 *     trouble), the two places it has and the extra places (extra_streaming.cpp) with their zones, the game's address space (a 32 bit process: 4 GB, of which a
 *     block of a few hundred MB must be free to load a section), and a `teleport` line for each player's car that puts it back where it is (the dev_tools command).
 *   - every 30 seconds of a race a line says how much is loaded and how much address space is left, so that a session of many races shows what grows;
 *   - the same dump is written when sections run out of memory (the streamer's count at +0x28 goes up).
 * The streamer's layout (the PC's: see agent_docs/split-screen.md): the sections table at +0, its count at +4, the counts of sections loaded / loading / activated /
 * out of memory at +0x1C / 0x20 / 0x24 / 0x28, the two places from +0xC0 (0x64 bytes each; `CurrentZone` at +0x1E), the pool's size at +0x83C. A section is 0x5C bytes:
 * status at +0xC (3 loaded, 4 activated) and size at +0x18.
 */
#include "streaming_log.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "engine_sounds.h"
#include "events.h"
#include "extra_streaming.h"
#include "keys.h"
#include "max_players.h"
#include "more_players.h"
#include "players.h"
#include "streaming_pool.h"
#include "split_screen_support.h"

namespace split_screen_support::streaming_log {

namespace {

constexpr uintptr_t kStreamer = 0x9AA9D0, kGameFlowState = 0x925E90;
constexpr unsigned kEntries = 0xC0, kEntrySize = 0x64, kZone = 0x1E, kPoolSize = 0x83C;
constexpr unsigned kSectionSize = 0x5C;

int g_key = VK_F11;
bool g_key_before = false;
unsigned g_frames = 0;
DWORD g_last_summary_ms = 0;
int g_last_out_of_memory = 0;

constexpr uintptr_t kCountFreeMemory = 0x464050, kLargestMalloc = 0x464080, kCarLoaderPool = 0x904A00;  // (CarLoaderMemoryPoolNumber)
constexpr int kPools = 8;

template <typename T>
T at(unsigned offset) { return *reinterpret_cast<T *>(kStreamer + offset); }

// The game's address space: what is used, what is free, and the biggest block that is (the streamer and the cars ask for blocks of that kind).
struct Space {
    long long used_mb, free_mb, largest_mb;
};

Space address_space() {
    MEMORYSTATUSEX status = {sizeof(status)};
    GlobalMemoryStatusEx(&status);
    Space space = {};
    space.free_mb = static_cast<long long>(status.ullAvailVirtual >> 20);
    space.used_mb = static_cast<long long>((status.ullTotalVirtual - status.ullAvailVirtual) >> 20);
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    unsigned long long largest = 0;
    for (unsigned char *address = static_cast<unsigned char *>(info.lpMinimumApplicationAddress); address < static_cast<unsigned char *>(info.lpMaximumApplicationAddress);) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery(address, &region, sizeof(region))) break;
        if (region.State == MEM_FREE && region.RegionSize > largest) largest = region.RegionSize;
        address = static_cast<unsigned char *>(region.BaseAddress) + region.RegionSize;
    }
    space.largest_mb = static_cast<long long>(largest >> 20);
    return space;
}

struct Sections {
    int total, loaded, loading, activated, out_of_memory;
    double loaded_mb;
};

Sections sections() {
    Sections s = {};
    s.total = at<int>(4);
    s.loaded = at<int>(0x1C);
    s.loading = at<int>(0x20);
    s.activated = at<int>(0x24);
    s.out_of_memory = at<int>(0x28);
    const unsigned char *table = at<const unsigned char *>(0);
    long long bytes = 0;
    if (table && s.total > 0 && s.total < 5000)
        for (int i = 0; i < s.total; ++i) {
            const unsigned char *section = table + i * kSectionSize;
            if (*reinterpret_cast<const int *>(section + 0xC) >= 3) bytes += *reinterpret_cast<const int *>(section + 0x18);
        }
    s.loaded_mb = bytes / 1048576.0;
    return s;
}

// The slot pool of the texture packs (see streaming_pool.cpp): how many are in use, the most so far and how many there can be.
void summary_pools() {
    const unsigned char *slots = *reinterpret_cast<unsigned char **>(0x916044);
    if (slots) g_log.line("streaming: texture pack slots: %d in use, %d at the most, %d in all", *reinterpret_cast<const int *>(slots + 0x18), *reinterpret_cast<const int *>(slots + 0x1C), *reinterpret_cast<const int *>(slots + 0x2C));
    g_log.line("streaming: the game's memory pools: %s", pools_text());
}

void summary(const char *why) {
    const Sections s = sections();
    const Space space = address_space();
    g_log.line("streaming: %s: pool %d MB, sections loaded %d of %d (%.0f MB; %d loading, %d activated, %d out of memory), address space: %lld MB used, %lld MB free, largest free block %lld MB", why,
               at<int>(kPoolSize) >> 20, s.loaded, s.total, s.loaded_mb, s.loading, s.activated, s.out_of_memory, space.used_mb, space.free_mb, space.largest_mb);
}

void dump(const char *why) {
    summary(why);
    summary_pools();
    engine_sounds::log_state(why);
    for (int slot = 0; slot < 2; ++slot) {
        const unsigned char *entry = reinterpret_cast<const unsigned char *>(kStreamer + kEntries + slot * kEntrySize);
        const float *place = reinterpret_cast<const float *>(entry);
        g_log.line("streaming: place %d of the streamer: zone %d, at (%.1f, %.1f) in the streamer's axes", slot + 1, *reinterpret_cast<const short *>(entry + kZone), place[0], place[1]);
    }
    int players[nfsmw::kMaxPlayers], zones[nfsmw::kMaxPlayers], slots[nfsmw::kMaxPlayers];
    const int extras = extra_streaming::extra_places(players, zones, slots, nfsmw::kMaxPlayers);
    for (int k = 0; k < extras; ++k) g_log.line("streaming: extra place %d: player %d, zone %d, sections go with place %d", k + 1, players[k] + 1, zones[k], slots[k] + 1);
    const int count = more_players::race_players();
    std::string lines;
    for (int i = 0; i < count; ++i) {
        float position[3], forward[3];
        if (!more_players::player_car_state(i, position, forward)) continue;
        char line[160];
        snprintf(line, sizeof(line), "teleport %d %.3f %.3f %.3f %.4f %.4f %.4f\n", i, position[0], position[1], position[2], forward[0], forward[1], forward[2]);
        lines += line;
    }
    if (!lines.empty()) g_log.line("streaming: where the players' cars are (a dev_tools scenario can put them back):\n%s", lines.c_str());
}

bool g_start_line = false;

void on_world_load() {
    g_last_out_of_memory = 0;
    g_last_summary_ms = GetTickCount();
    g_start_line = true;
    summary("a race is loading");
}

void on_frame() {
    if (++g_frames == 1) {  // (the game has made the pool by its first frame)
        streaming_pool::report();
        g_log.line("streaming: the car loader's pool is number %d, the size the game gave it %d MB", *reinterpret_cast<int *>(kCarLoaderPool), *reinterpret_cast<int *>(0x8F7EF0) >> 10);
    }
    if (nfsmw::key_clicked(g_key, g_key_before)) dump("dump asked for");
    if (g_start_line && *reinterpret_cast<int *>(kGameFlowState) == 6 && GetTickCount() - g_last_summary_ms >= 12000) {  // the start of the race, with the cars close together
        g_start_line = false;
        summary("a few seconds into the race");
    }
    if (g_frames % 60 != 0 || !players::split_screen() || *reinterpret_cast<int *>(kGameFlowState) != 6) return;
    const int out_of_memory = at<int>(0x28);
    if (out_of_memory != g_last_out_of_memory) {
        g_last_out_of_memory = out_of_memory;
        if (out_of_memory > 0) dump("sections out of memory");
    }
    const DWORD now = GetTickCount();
    if (now - g_last_summary_ms >= 30000) {
        g_last_summary_ms = now;
        summary("every 30 seconds");
        summary_pools();
    }
}

}  // namespace

int pool_free(int pool) { return reinterpret_cast<int(__cdecl *)(int)>(kCountFreeMemory)(pool); }
int pool_largest(int pool) { return reinterpret_cast<int(__cdecl *)(int)>(kLargestMalloc)(pool); }

const char *pools_text() {
    static char text[400];
    int n = 0;
    text[0] = 0;
    for (int pool = 0; pool < kPools && n < static_cast<int>(sizeof(text)) - 80; ++pool) {
        const int free_bytes = pool_free(pool), largest = pool_largest(pool);
        if (free_bytes <= 0 && largest <= 0) continue;
        const char *what = pool == *reinterpret_cast<int *>(kCarLoaderPool) ? " (the car loader's)" : pool == 7 ? " (the track streamer's)" : "";
        n += snprintf(text + n, sizeof(text) - n, "%spool %d%s: %d MB free (largest block %d MB)", n ? "; " : "", pool, what, free_bytes >> 20, largest >> 20);
    }
    return text;
}

void install() {
    g_key = static_cast<int>(GetPrivateProfileIntA("options", "streaming_dump_key", VK_F11, g_ini));
    events::on_world_load(&on_world_load);
    events::on_frame(&on_frame);
}

}  // namespace split_screen_support::streaming_log

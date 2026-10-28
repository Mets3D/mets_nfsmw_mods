/*
 * The track streamer keeps what it has loaded of the track in one block of memory, its pool (TrackStreamer::InitMemoryPool 0x72D9C0: the size at +0x83C, the block from
 * bMalloc at +0x838, the pool object at +0x844). `LoadGlobalChunks` makes it once, when the game starts (0x664A4F): 0x7EF4000 (0x7EB4000 under one flag) plus `[0x8F7EF0]`
 * KB, which is also the size of the car loader's memory (CarLoader::SetMemoryPoolSize, the next call); 242 MB with the HD content of Redux v3. A race of four players with
 * cars far apart fills it (54 sections, 213 MB, for two places: 188 MB of them are the three that every place has; each place of its own costs 8 to 18 MB), and the whole
 * track is 576 MB. By the time a plugin's main runs (a thread that the DLL starts) the game has made it, with other things already allocated in it (125 MB at the first
 * frame), so it cannot be replaced then; but the DLL is loaded before the game's own code runs, and its static initializer patches the code that computes the size:
 * the `add eax, esi; push eax; mov ecx, streamer` of 0x664A47 (8 bytes) jumps to a stub that calls choose_pool_size(the game's size) and pushes what it returns.
 * The block is in the game's address space (32 bits: 4 GB at most, a race of four players uses 3.6 GB of it with a pool of 1 GB), and it is one contiguous piece: a
 * pool that left too little room made a later allocation of the game's fail (a null destination buffer in SplitPermTempChunks 0x64A9C0: a crash at start). So the size
 * is chosen when the game makes the pool (that is the address space the game has then, which differs from one start to another, with the other plugins and the drivers
 * allocating on their own threads): the biggest size, down to the game's own, up to `[options] streaming_pool_mb` (512 by default; 0 = the game's own), that leaves
 * `[options] streaming_pool_keep_mb` (2400) of address space free in all and a free block of 600 MB. The game has 3.4 GB free when it makes the pool [tested: 3377 to 3401 MB, the
 * largest block 2046 MB] and allocates about 1.9 GB more in a race of four players: with a pool of 1 GB 455 MB were free at the end.
 */
#include "streaming_pool.h"

#include <windows.h>

#include "nfsmw_util.h"
#include "patch.h"
#include "split_screen_support.h"

namespace split_screen_support::streaming_pool {

namespace {

constexpr uintptr_t kSite = 0x664A47, kInitMemoryPoolCall = 0x664A4F, kStreamer = 0x9AA9D0;
constexpr long long kMB = 1048576;
constexpr long long kKeepBlockMB = 600;  // the largest free block that must be left

int g_wanted_mb = 0, g_keep_mb = 0;
bool g_patched = false;

// What choose_pool_size found (it runs before the log is open).
struct Result {
    bool ran = false;
    long long game_mb = 0, chosen_mb = 0;
    long long free_before_mb = 0, largest_before_mb = 0, free_after_mb = 0, largest_after_mb = 0;
} g_result;

struct Space {
    long long free_mb, largest_mb;
};

Space measure() {
    MEMORYSTATUSEX status = {sizeof(status)};
    GlobalMemoryStatusEx(&status);
    Space space = {static_cast<long long>(status.ullAvailVirtual / kMB), 0};
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    unsigned long long largest = 0;
    for (unsigned char *address = static_cast<unsigned char *>(info.lpMinimumApplicationAddress); address < static_cast<unsigned char *>(info.lpMaximumApplicationAddress);) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery(address, &region, sizeof(region))) break;
        if (region.State == MEM_FREE && region.RegionSize > largest) largest = region.RegionSize;
        address = static_cast<unsigned char *>(region.BaseAddress) + region.RegionSize;
    }
    space.largest_mb = static_cast<long long>(largest / kMB);
    return space;
}

// Called by the stub with the size the game would make the pool (bytes); returns the size it is to be.
int __cdecl choose_pool_size(int game_size) {
    g_result.ran = true;
    g_result.game_mb = game_size / kMB;
    g_result.chosen_mb = g_result.game_mb;
    const Space before = measure();
    g_result.free_before_mb = before.free_mb;
    g_result.largest_before_mb = before.largest_mb;
    g_result.free_after_mb = before.free_mb;
    g_result.largest_after_mb = before.largest_mb;
    for (long long mb = g_wanted_mb; mb * kMB > game_size; mb -= 64) {
        void *probe = VirtualAlloc(nullptr, static_cast<SIZE_T>(mb * kMB), MEM_RESERVE, PAGE_NOACCESS);
        if (!probe) continue;
        const Space after = measure();  // with the block taken
        VirtualFree(probe, 0, MEM_RELEASE);
        if (after.free_mb >= g_keep_mb && after.largest_mb >= kKeepBlockMB) {
            g_result.chosen_mb = mb;
            g_result.free_after_mb = after.free_mb;
            g_result.largest_after_mb = after.largest_mb;
            return static_cast<int>(mb * kMB);
        }
    }
    return game_size;
}

// The texture packs: `eInitTextures` (0x4FCD00) makes the slot pool that every TexturePack object comes from, `bNewSlotPool(0x20, 0x80, name, 0)`: 128 slots of 32 bytes, so 128 texture
// packs at a time (the cars, the minimap tiles, the track's texture sections, the HUD...). A slot pool that is full returns null and `TexturePack::AttachTextureTable` 0x506090 crashed on it
// [tested: the stack of two crashes in a long session of 4 players driving apart: 0x5673F8 UncompressMaps and 0x727E22 ActivateSection, the second with 77 sections loaded], so the
// count is `[options] texture_pack_slots` (1024 by default: 32 KB; 0 = the game's own). The pool object is at [0x916044]: +0x18 in use, +0x1C the most used, +0x2C the capacity.
constexpr uintptr_t kTexturePackSlotsPush = 0x4FCD07;
void patch_texture_pack_slots(const char *ini) {
    const int slots = static_cast<int>(GetPrivateProfileIntA("options", "texture_pack_slots", 1024, ini));
    if (slots <= 0x80) return;
    static const unsigned char expected[5] = {0x68, 0x80, 0x00, 0x00, 0x00};  // push 0x80
    unsigned char patched[5] = {0x68};
    memcpy(patched + 1, &slots, 4);
    nfsmw::patch_bytes(kTexturePackSlotsPush, expected, patched, sizeof(patched));
}

struct Early {
    Early() {
        char ini[MAX_PATH];
        nfsmw::data_file("nfsmw_split_screen_support.ini", ini, sizeof(ini));
        g_wanted_mb = static_cast<int>(GetPrivateProfileIntA("options", "streaming_pool_mb", 512, ini));
        g_keep_mb = static_cast<int>(GetPrivateProfileIntA("options", "streaming_pool_keep_mb", 2400, ini));
        patch_texture_pack_slots(ini);
        if (g_wanted_mb <= 0) return;
        static const unsigned char expected[8] = {0x03, 0xC6, 0x50, 0xB9, 0xD0, 0xA9, 0x9A, 0x00};  // add eax, esi; push eax; mov ecx, 0x9AA9D0
        nfsmw::Stub stub;
        stub.emit({0x03, 0xC6});                                                    // add eax, esi (the game's size)
        stub.emit({0x50});                                                          // push eax
        stub.emit_rel32(0xE8, reinterpret_cast<uintptr_t>(&choose_pool_size));      // call choose_pool_size (cdecl; esi, edi, ebx, ebp are kept)
        stub.emit({0x83, 0xC4, 0x04});                                              // add esp, 4
        stub.emit({0x50});                                                          // push eax (the size)
        stub.emit({0xB9});                                                          // mov ecx, the streamer
        stub.emit_u32(kStreamer);
        stub.emit_rel32(0xE9, kInitMemoryPoolCall);                                 // jmp to the call of InitMemoryPool
        stub.done();
        g_patched = nfsmw::jump_to_stub(kSite, expected, sizeof(expected), stub);
    }
} g_early;

}  // namespace

void report() {
    if (g_wanted_mb <= 0) {
        g_log.line("streaming pool: [options] streaming_pool_mb is 0: the game's own size");
    } else if (!g_patched) {
        g_log.line("streaming pool: not changed (the code at %#x is not as expected)", static_cast<unsigned>(kSite));
    } else if (!g_result.ran) {
        g_log.line("streaming pool: the game has not made it yet");
    } else {
        g_log.line("streaming pool: %lld MB (the game's own %lld MB; up to %d MB wanted, %d MB of address space to keep free): the address space was %lld MB free, the largest block %lld MB; "
                   "with the pool %lld MB free, the largest block %lld MB",
                   g_result.chosen_mb, g_result.game_mb, g_wanted_mb, g_keep_mb, g_result.free_before_mb, g_result.largest_before_mb, g_result.free_after_mb, g_result.largest_after_mb);
    }
}

}  // namespace split_screen_support::streaming_pool

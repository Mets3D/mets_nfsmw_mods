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

#include <algorithm>

#include "nfsmw_util.h"
#include "patch.h"
#include "racers.h"
#include "split_screen_support.h"

namespace split_screen_support::streaming_pool {

namespace {

constexpr uintptr_t kSite = 0x664A47, kInitMemoryPoolCall = 0x664A4F, kStreamer = 0x9AA9D0;
constexpr long long kMB = 1048576;
constexpr long long kKeepBlockMB = 600;  // the largest free block that must be left

int g_wanted_mb = 0, g_car_extra_mb = 0;
constexpr int kKeepMb = 2400;  // the address space the pool leaves free (see choose_pool_size)
bool g_patched = false;

// The car loader's memory (CarLoader::SetMemoryPoolSize 0x75C550: bMalloc(size, 7), a pool of its own made in that block) comes out of the track streamer's pool (pool 7), for which
// it first evicts track sections (TrackStreamer::MakeSpaceInPool). Its size is set when the game starts (0x664A64) and again when each track loads (TrackLoader::LoadHandler,
// 0x666EEA: `[0x8F7EF0]` KB, 115 MB with Redux v3's HD content) and by 0x75DA62. Sixteen racers of different models fill 115 MB: the data of the cars loaded first is evicted
// and they are drawn as black frames [tested: 12 MB free in a race of 16]. The size of every call gets `[options] car_pool_extra_mb`, and the block is one of ours, reserved before
// the track pool's size is chosen (so the address space it leaves counts it), which keeps it out of the track pool; MakeSpaceInPool is not called for it.
constexpr uintptr_t kSetCarPoolCalls[] = {0x664A64, 0x666EEA, 0x75DA62}, kCarMakeSpaceCall = 0x75C5EC, kCarMallocCall = 0x75C5F4, kCarFreeCall = 0x75C5C8;
constexpr uintptr_t kCarPoolKB = 0x8F7EF0, kCarPoolKBOther = 0x8F7EF4;
uintptr_t g_set_car_pool[3] = {}, g_make_space = 0, g_car_malloc = 0, g_car_free = 0;
void *g_car_block = nullptr;
int g_car_block_size = 0;

void reserve_car_block() {
    if (g_car_extra_mb <= 0 || g_car_block) return;
    const int game_kb = std::max(*reinterpret_cast<int *>(kCarPoolKB), *reinterpret_cast<int *>(kCarPoolKBOther));
    const int size = (game_kb << 10) + (g_car_extra_mb << 20);
    g_car_block = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (g_car_block) g_car_block_size = size;
}

template <int N>
void __fastcall set_car_pool(void *loader, void *edx, int size) {
    reinterpret_cast<void(__fastcall *)(void *, void *, int)>(g_set_car_pool[N])(loader, edx, size > 0 ? size + (g_car_extra_mb << 20) : size);
}

bool __fastcall car_make_space(void *streamer, void *edx, int size, bool flag) {
    if (g_car_block && size <= g_car_block_size) return true;
    return reinterpret_cast<bool(__fastcall *)(void *, void *, int, bool)>(g_make_space)(streamer, edx, size, flag);
}

void *__cdecl car_malloc(int size, int flags) {
    if (g_car_block && size <= g_car_block_size) return g_car_block;
    return reinterpret_cast<void *(__cdecl *)(int, int)>(g_car_malloc)(size, flags);
}

void __cdecl car_free(void *block) {
    if (block && block == g_car_block) return;  // kept for the next size
    reinterpret_cast<void(__cdecl *)(void *)>(g_car_free)(block);
}

void patch_car_pool() {
    if (g_car_extra_mb <= 0) return;
    const uintptr_t targets[3] = {reinterpret_cast<uintptr_t>(&set_car_pool<0>), reinterpret_cast<uintptr_t>(&set_car_pool<1>), reinterpret_cast<uintptr_t>(&set_car_pool<2>)};
    for (int i = 0; i < 3; ++i) g_set_car_pool[i] = nfsmw::chain_call(kSetCarPoolCalls[i], targets[i]);
    g_make_space = nfsmw::chain_call(kCarMakeSpaceCall, reinterpret_cast<uintptr_t>(&car_make_space));
    g_car_malloc = nfsmw::chain_call(kCarMallocCall, reinterpret_cast<uintptr_t>(&car_malloc));
    g_car_free = nfsmw::chain_call(kCarFreeCall, reinterpret_cast<uintptr_t>(&car_free));
}

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
    reserve_car_block();
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
        if (after.free_mb >= kKeepMb && after.largest_mb >= kKeepBlockMB) {
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
// count is 1024 (32 KB). The pool object is at [0x916044]: +0x18 in use, +0x1C the most used, +0x2C the capacity.
constexpr uintptr_t kTexturePackSlotsPush = 0x4FCD07;
constexpr int kTexturePackSlots = 1024;
void patch_texture_pack_slots() {
    const int slots = kTexturePackSlots;
    static const unsigned char expected[5] = {0x68, 0x80, 0x00, 0x00, 0x00};  // push 0x80
    unsigned char patched[5] = {0x68};
    memcpy(patched + 1, &slots, 4);
    nfsmw::patch_bytes(kTexturePackSlotsPush, expected, patched, sizeof(patched));
}

// The sound system's memory: `InitializeSoundDriver` (0x4CC3A0) makes one memory pool for all of it, `bMalloc(0x563000 + 0x4000)` (5.4 MB, `and eax, 0x563000` at 0x4CC3A9
// when sound is on) given to `bInitMemoryPool` as `AudioMemoryPool` [0x8EE558]. The engine banks of the opponents are loaded into it (`EAXAemsManager::InitiateLoad` 0x4CFFA0 fails with
// -4 when its largest free block is smaller than the bank; `AsyncResidentAllocCB` 0x4CFB20), and so are the Csis middleware's objects, the bank slots' buffers and the mixer's memory.
// An engine whose bank is not loaded is never initialized (its `InitSFX`, which makes its Csis::CAR object, waits for the bank): with 2 players and 4 opponents none of the
// opponents had a Csis::CAR object and none was heard, with 3 all were [tested], and a bigger pool of Csis objects taken from the same memory made 2 the most. 2 MB for
// each racer of `[options] max_racers` (32 MB for 16; the game's own with 4) is the size.
constexpr uintptr_t kAudioPoolSize = 0x4CC3AA;
void patch_audio_pool(int most) {
    const int mb = most > 4 ? 2 * most : 0;  // 32 MB for 16 racers
    if (mb <= 6) return;
    const int value = mb * 1048576 - 0x4000;
    static const unsigned char expected[4] = {0x00, 0x30, 0x56, 0x00};  // the 0x563000 of `and eax, 0x563000`
    unsigned char patched[4];
    memcpy(patched, &value, 4);
    nfsmw::patch_bytes(kAudioPoolSize, expected, patched, sizeof(patched));
}

// The objects of the Csis middleware: `EAXSND8Wrapper::Initialize` (0x4C40C0) makes one slot pool for everything it allocates (`Csis::System::Alloc` is `bOMalloc(pCsisSlotPools)`:
// one slot of the pool's size whatever is asked for), `bNewSlotPool(slot size, 0x100, "AUD:Csis SlotPools", AudioMemoryPool)`: 256. Each engine sound takes several (its Csis::CAR object, its
// class instance ...): 199 of them were in use in a race of 2 players and 14 opponents [tested], and a full pool returns null, which leaves an engine without its Csis::CAR object: silent.
// 1024 is the count; the pool is taken from the sound system's memory (above).
constexpr uintptr_t kCsisSlotsPush = 0x4C411E;
constexpr int kCsisSlots = 1024;
void patch_csis_slots() {
    const int slots = kCsisSlots;
    static const unsigned char expected[5] = {0x68, 0x00, 0x01, 0x00, 0x00};  // push 0x100
    unsigned char patched[5] = {0x68};
    memcpy(patched + 1, &slots, 4);
    nfsmw::patch_bytes(kCsisSlotsPush, expected, patched, sizeof(patched));
}

// The sound hardware's voices (one playing sample each; an engine plays several): `EAXSND8Wrapper::Initialize` asks for 64 (`push 0x40` at 0x4C4213, `Snd6System::SetVoices(0, 64)`),
// and the library makes its voice table that big (clamped to its own most, 255 on this machine [tested: logged]). A race of 2 players and 14 opponents used 48 at once [tested].
// 128 is the count.
constexpr uintptr_t kVoicesPush = 0x4C4213;
constexpr int kSoundVoices = 128;
void patch_voices() {
    const int voices = kSoundVoices;
    static const unsigned char expected[2] = {0x6A, 0x40};  // push 0x40 (SetVoices takes the low byte)
    const unsigned char patched[2] = {0x6A, static_cast<unsigned char>(voices)};
    nfsmw::patch_bytes(kVoicesPush, expected, patched, sizeof(patched));
}

// The render contexts of the menus and HUDs: `cFEngRender` (made once by `FEManager::Init`: `malloc(0x7BD0)` at 0x54EB2D, the object at [0x91CFA8]) ends in RContexts[180] (0xB0
// bytes each from +0x10, cleared by its constructor 0x591AF0: `mov ecx, 0x1EF0` dwords at 0x591B35), and `cFEngRender::GenerateRenderContext` 0x585320 fills one for every group
// drawn in a frame (the index is FEngine +0xD4, a 16 bit counter that `FEngine::Render` sets to 0 before all the packages) with no check of the end. Four split screen HUDs with
// their leaderboard and mirror frame copies, the pause menu and Options > Video went past 180 and wrote over the heap after the object [tested: dev_tools' heap watch caught
// GenerateRenderContext and MakeRenderMatrix writing into the package manager's entries; the game then crashed in the manager's walks of its list]. Everything indexes the
// array from +0x10 and nothing follows it, so a bigger object is a bigger array: 1024 contexts (176 KB).
constexpr uintptr_t kRenderAllocPush = 0x54EB2D, kRenderClearCount = 0x591B35, kRender = 0x91CFA8;
constexpr unsigned kRenderContexts = 1024, kRenderContextSize = 0xB0, kRenderArray = 0x10;
bool g_render_contexts_patched = false;
void patch_render_contexts() {
    static const unsigned char alloc_expected[5] = {0x68, 0xD0, 0x7B, 0x00, 0x00};  // push 0x7BD0
    static const unsigned char clear_expected[5] = {0xB9, 0xF0, 0x1E, 0x00, 0x00};  // mov ecx, 0x1EF0
    if (memcmp(reinterpret_cast<const void *>(kRenderAllocPush), alloc_expected, 5) != 0 || memcmp(reinterpret_cast<const void *>(kRenderClearCount), clear_expected, 5) != 0)
        return;  // (both or neither)
    const unsigned size = kRenderArray + kRenderContexts * kRenderContextSize, dwords = kRenderContexts * kRenderContextSize / 4;
    unsigned char alloc[5] = {0x68}, clear[5] = {0xB9};
    memcpy(alloc + 1, &size, 4);
    memcpy(clear + 1, &dwords, 4);
    g_render_contexts_patched = nfsmw::patch_bytes(kRenderAllocPush, alloc_expected, alloc, 5) && nfsmw::patch_bytes(kRenderClearCount, clear_expected, clear, 5);
}

struct Early {
    Early() {
        char ini[MAX_PATH];
        nfsmw::data_file("nfsmw_split_screen_support.ini", ini, sizeof(ini));
        g_wanted_mb = static_cast<int>(GetPrivateProfileIntA("options", "streaming_pool_mb", 512, ini));
        const int most = racers::max_racers(ini);
        g_car_extra_mb = most > 4 ? 8 * most : 0;  // 128 MB for 16 racers
        patch_texture_pack_slots();
        patch_audio_pool(most);
        patch_csis_slots();
        patch_voices();
        patch_car_pool();
        patch_render_contexts();
        if (g_wanted_mb <= 0 && g_car_extra_mb <= 0) return;
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
    // (the size of the object's block, from the debug heap's header before it: whether the game made it after the patch)
    const unsigned char *render = *reinterpret_cast<const unsigned char *const *>(kRender);
    const unsigned block = render ? *reinterpret_cast<const unsigned *>(render - 0x10) : 0;
    g_log.line("menus: %s; the renderer's object is %s", g_render_contexts_patched ? "1024 render contexts (the game's 180)" : "the game's 180 render contexts (the code is not as expected)",
               !render ? "not made yet" : block == kRenderArray + kRenderContexts * kRenderContextSize ? "the bigger one" : "the game's size");
    if (g_car_extra_mb > 0) {
        const bool redirected = g_set_car_pool[0] && g_set_car_pool[1] && g_set_car_pool[2] && g_make_space && g_car_malloc && g_car_free;
        g_log.line("streaming pool: the car loader's pool gets %d MB more than the game's (8 MB for each racer of [options] max_racers), in a block of its own: %s", g_car_extra_mb,
                   !redirected ? "not all of the game's calls are as expected" : g_car_block ? "reserved" : "could not be reserved (it comes from the track pool, as in the game)");
    }
    if (g_wanted_mb <= 0) {
        g_log.line("streaming pool: [options] streaming_pool_mb is 0: the game's own size");
    } else if (!g_patched) {
        g_log.line("streaming pool: not changed (the code at %#x is not as expected)", static_cast<unsigned>(kSite));
    } else if (!g_result.ran) {
        g_log.line("streaming pool: the game has not made it yet");
    } else {
        g_log.line("streaming pool: %lld MB (the game's own %lld MB; up to %d MB wanted, %d MB of address space to keep free): the address space was %lld MB free, the largest block %lld MB; "
                   "with the pool %lld MB free, the largest block %lld MB",
                   g_result.chosen_mb, g_result.game_mb, g_wanted_mb, kKeepMb, g_result.free_before_mb, g_result.largest_before_mb, g_result.free_after_mb, g_result.largest_after_mb);
    }
}

}  // namespace split_screen_support::streaming_pool
